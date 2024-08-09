#include <unistd.h>

int main() {
    char c;
    // Read from standard input byte by byte, enter 'q' to quit
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;
}