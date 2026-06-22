// EXPECT-ERRORS: 1
// EXPECT: expect expression
// EXPECT-EXTERNALS: 1
// EXPECT-MAIN-STATEMENTS: 2
int main() {
    int broken = ;
    return 42;
}
