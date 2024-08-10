#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>

struct termios orig_termios;

void crash(const char *s) {
    // Print descriptive error
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) crash("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) crash("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    // Disable Ctrl-M, Ctrl-S and Ctrl-Q
    raw.c_lflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Disable logging new line
    raw.c_oflag &= ~(OPOST);

    // Set character size to 8 bits per length
    raw.c_cflag |= (CS8);

    // Disable X, X, Ctrl-V, X
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
         if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) crash("read");

        // Only print out printable characters
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }

    return 0;
}