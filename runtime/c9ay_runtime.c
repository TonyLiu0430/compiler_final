#include <c9ay.h>

int main();

void *GetStdHandle(int handle);
int WriteFile(
    void *file,
    void *buffer,
    int size,
    int *written,
    void *overlapped);
void ExitProcess(unsigned int exit_code);

void __main() {
}

void _start() {
    ExitProcess(main());
}

static int textLength(char *text) {
    int length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}

void writeText(char *text) {
    int written = 0;
    WriteFile(
        GetStdHandle(-11),
        text,
        textLength(text),
        &written,
        0);
}

void writeChar(char value) {
    int written = 0;
    WriteFile(
        GetStdHandle(-11),
        &value,
        1,
        &written,
        0);
}

void print(char *text) {
    writeText(text);
}

void printLine(char *text) {
    writeText(text);
    writeChar('\n');
}

void printInt(int value) {
    char digits[12];
    int count = 0;
    int negative = value < 0;

    if (!negative) {
        value = -value;
    }

    do {
        int digit = -(value % 10);
        digits[count] = '0' + digit;
        count++;
        value = value / 10;
    } while (value);

    if (negative) {
        writeChar('-');
    }

    while (count > 0) {
        count--;
        writeChar(digits[count]);
    }
}

void printIntLine(int value) {
    printInt(value);
    writeChar('\n');
}
