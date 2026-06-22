// EXPECT-ERRORS: 1
// EXPECT: expect expression
// EXPECT-EXTERNALS: 1
// EXPECT-MAIN-STATEMENTS: 2
int main() {
    if ()
        return 1;
    return 42;
}
