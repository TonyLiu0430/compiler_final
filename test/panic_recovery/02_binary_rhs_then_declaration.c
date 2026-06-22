// EXPECT-ERRORS: 1
// EXPECT: expect expression
// EXPECT-EXTERNALS: 1
// EXPECT-MAIN-STATEMENTS: 3
int main() {
    int value = 0;
    value = 1 + ;
    int preserved = 42;
}
