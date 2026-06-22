// EXPECT-ERRORS: 1
// EXPECT: expect expression
// EXPECT-EXTERNALS: 1
// EXPECT-MAIN-STATEMENTS: 3
int main() {
    int values[2] = {1, 2};
    values[];
    return 42;
}
