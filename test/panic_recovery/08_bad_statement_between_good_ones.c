// EXPECT-ERRORS: 1
// EXPECT: expect expression
// EXPECT-EXTERNALS: 1
// EXPECT-MAIN-STATEMENTS: 3
int main() {
    int before = 1;
    before = ;
    return before;
}
