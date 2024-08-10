#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

void crash(const char *s) {
    // Print descriptive error
    perror(s);
    exit(1);
}

void disableRawMode() {
    // Check if tcsetattr() returns -1 (when an error occurs) and return error message
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) crash("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) crash("tcgetattr");

    // Register function that executes when the program exits normally
    atexit(disableRawMode);

    struct termios raw = orig_termios;
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

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) crash("tcsetattr");
}

int main() {
    enableRawMode();

    // Read from standard input byte by byte, enter 'q' to quit
    while (1) {
        char c = '\0';
        // Error handling
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) crash("read");

        // Only print out printable characters
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == CTRL_KEY('q')) break;
    }

    return 0;
}