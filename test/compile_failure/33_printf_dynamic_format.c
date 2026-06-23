// EXPECT-ERRORS: 1
// EXPECT: printf format must be a string literal
int main() {
    char *format = "%d";
    printf(format, 42);
    return 0;
}
