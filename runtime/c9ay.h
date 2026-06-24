#ifndef C9AY_H
#define C9AY_H

void write_text(char *text);
void write_char(char value);

void print(char *text);
void println(char *text);
void print_int(int value);
void print_long_long(long long value);
void print_unsigned_long_long(unsigned long long value);
void print_float(double value);

int printf(char *format, ...);

#endif
