#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define APPBUF_INIT {NULL, 0}

enum editorKey {
    LEFT = 1000,
    RIGHT,
    UP,
    DOWN,
    DEL,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN
};

typedef struct editorRow {
    int size;
    char *chars;
}   editorRow;

struct editorConfig {
    // Cursor positions
    int cx, cy;

    int screenrows;
    int screencols;
    
    int numrows;
    editorRow row;

    struct termios orig_termios;
};

struct editorConfig editor;

// Dynamic buffer
struct appBuf {
    char *b;
    int len;
};

void bAppend(struct appBuf *ab, const char *s, int len) {
    // Rezise the existing buffer to accomodate additional len bytes
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void bFree(struct appBuf *ab) {
    free(ab->b);
}

void crash(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    // Print descriptive error
    perror(s);
    exit(1);
}

void disableRawMode() {
    // Check if tcsetattr() returns -1 (when an error occurs) and return error message
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.orig_termios) == -1) crash("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &editor.orig_termios) == -1) crash("tcgetattr");

    // Register function that executes when the program exits normally
    atexit(disableRawMode);

    struct termios raw = editor.orig_termios;
    // c_lflag contains various local mode flags that control terminal behaviour
    // &= ~ (Bitwise AND with a NOT to flip specific switches in the c_lflag)

    // Disable: Interrupt signals (Ctrl-C), parity checking, stripping of high-order bits, carriage return mapping (Ctrl-M), software flow control (Ctrl-S and Ctrl-Q)
    raw.c_lflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // c_oflag controls output processing flags for the terminal
    // Disable logging new line (output processing)
    raw.c_oflag &= ~(OPOST);

    // c_cflag controls hardware and data format settings for terminal I/O
    // Set character size to 8 bits per length
    raw.c_cflag |= (CS8);

    // Disable: input characters echoed to screen, input processed line-by-line, Ctrl-V, Special characters (Ctrl-Z) generating signals
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // minimum number of bytes of input needed before read() can return
    raw.c_cc[VMIN] = 0;

    // maximum amount of time to wait before read() returns
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.orig_termios) == -1) crash("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) crash("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME;
                        case '3': return DEL;
                        case '4': return END;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME;
                        case '8': return END;
                    }
                }
            } else {
                switch (seq[1])
                {
                    case 'A': return UP;
                    case 'B': return DOWN;
                    case 'C': return RIGHT;
                    case 'D': return LEFT;
                    case 'H': return HOME;
                    case 'F': return END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME;
                case 'F': return END;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}


int getCursorPosition(int *rows, int *cols) {
    // Hold respond of terminal in buffer and keep track of position in buffer
    char buf[32];
    unsigned int i = 0;

    // Send escape sequence to request cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    // Read response from standard input
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    
    // Check if response starts with the expected escape sequence
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;

    // Parse row and column numbers from response
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Go to bottom right of screen and fill up 
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) crash("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    // Read file
    linelen = getline(&line, &linecap, fp);
    if (linelen != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        editor.row.size = linelen;
        editor.row.chars = malloc(linelen + 1);
        memcpy(editor.row.chars, line, linelen);
        editor.row.chars[linelen] = '\0';
        editor.numrows = 1;
    }
    free(line);
    fclose(fp);
}

void editorMoveCursor(int key) {
    switch (key) {
        case LEFT:
            if (editor.cx != 0) {
                editor.cx--;
            }
            break;
        case RIGHT:
            if (editor.cx != editor.screencols - 1) {
                editor.cx++;
            }     
            break;
        case UP:
            if (editor.cy != 0) {
                    editor.cy--;
            }
            break;
        case DOWN:
            if (editor.cy != editor.screenrows - 1) {
                editor.cy++;
                break;
            }         
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();
    switch (c) {
        case CTRL_KEY('b'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        
        case HOME:
            editor.cx = 0;
            break;

        case END:
            editor.cx = editor.screencols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int time = editor.screenrows;
                while (time--) editorMoveCursor(c == PAGE_UP ? UP : DOWN);
            }
            break;
        case UP:
        case DOWN:
        case LEFT:
        case RIGHT:
            editorMoveCursor(c);
            break;
    }
}

void editorDrawRows(struct appBuf *ab) {
    int y;
    for (y = 0; y < editor.screenrows; y++) {
        if (y >= editor.numrows) {
            if (y == editor.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Welcome");
                if (welcomelen > editor.screencols) welcomelen = editor.screencols;
                // Move welcome message to center of screen.
                int padding = (editor.screencols - welcomelen) / 2;
                if (padding) {
                    bAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) bAppend(ab, " ", 1);
                bAppend(ab, welcome, welcomelen);
            } else {
                bAppend(ab, "~", 1);
            }
        } else {
            int len = editor.row.size;
            if (len > editor.screencols) len = editor.screencols;
            bAppend(ab, editor.row.chars, len);
        }

        bAppend(ab, "\x1b[K", 3);
        if (y < editor.screenrows - 1) {
            bAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct appBuf ab = APPBUF_INIT;
    bAppend(&ab, "\x1b[?25l", 6);

    // Position cursor to top-left corner
    bAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH]", editor.cy + 1, editor.cx + 1);
    bAppend(&ab, buf, strlen(buf));

    bAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    bFree(&ab);
}

void initEditor() {
    editor.cx = 0;
    editor.cy = 0;
    editor.numrows = 0;

    if (getWindowSize(&editor.screenrows, &editor.screencols) == -1) crash("getWindowSize");
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}