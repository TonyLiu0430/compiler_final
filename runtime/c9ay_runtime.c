#include <c9ay.h>

int main();

void* GetStdHandle(int handle);
int WriteFile(void* file, void* buffer, int size, int* written, void* overlapped);
void ExitProcess(unsigned int exit_code);

void __main() {}

void _start() {
    ExitProcess(main());
}

static int textLength(char* text) {
    int length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}

void write_text(char* text) {
    int written = 0;
    WriteFile(GetStdHandle(-11), text, textLength(text), &written, 0);
}

void write_char(char value) {
    int written = 0;
    WriteFile(GetStdHandle(-11), &value, 1, &written, 0);
}

void print(char* text) {
    write_text(text);
}

void println(char* text) {
    write_text(text);
    write_char('\n');
}

void print_int(int value) {
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
        write_char('-');
    }

    while (count > 0) {
        count--;
        write_char(digits[count]);
    }
}

void print_long_long(long long value) {
    char digits[20];
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
        write_char('-');
    }

    while (count > 0) {
        count--;
        write_char(digits[count]);
    }
}

void print_unsigned_long_long(unsigned long long value) {
    char digits[20];
    int count = 0;

    do {
        int digit = value % 10;
        digits[count] = '0' + digit;
        count++;
        value = value / 10;
    } while (value);

    while (count > 0) {
        count--;
        write_char(digits[count]);
    }
}

void print_float(double value) {
    int i = 0;
    if (value < 0.0) {
        write_char('-');
        value = -value;
    }

    value = value + 0.0000005;
    unsigned long long whole = value;
    double fraction = value - whole;

    print_unsigned_long_long(whole);
    write_char('.');

    while (i < 6) {
        fraction = fraction * 10.0;
        int digit = fraction;
        write_char('0' + digit);
        fraction = fraction - digit;
        i++;
    }
}
