#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define APPBUF_INIT {NULL, 0}

struct editorConfig {
    int screenrows;
    int screencols;

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

char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) crash("read");
    }
    return c;
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

void editorProcessKeypress() {
    char c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

void editorDrawRows(struct appBuf *ab) {
    int y;
    for (y = 0; y < editor.screenrows; y++) {
        bAppend(ab, "~", 1);

        if (y < editor.screenrows - 1) {
            bAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct appBuf ab = APPBUF_INIT;
    bAppend(&ab, "\x1b[?25l", 6);

    // Clear entire screen
    bAppend(&ab, "\x1b[2J", 4);

    // Position cursor to top-left corner
    bAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    bAppend(&ab, "\x1b[H", 3);
    bAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    bFree(&ab);
}

void initEditor() {
    if (getWindowSize(&editor.screenrows, &editor.screencols) == -1) crash("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}