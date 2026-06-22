// EXPECT-ERRORS: 1
// EXPECT: expect expression
// EXPECT-EXTERNALS: 3
// EXPECT-MAIN-STATEMENTS: 1
int broken = ;
int preserved = 42;

int main() {
    return preserved;
}
