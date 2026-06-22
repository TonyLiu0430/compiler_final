// EXPECT-ERRORS: 1
// EXPECT: expect expression
// EXPECT-EXTERNALS: 2
// EXPECT-MAIN-STATEMENTS: 2
int function(int value) {
    return value;
}

int main() {
    function(1, );
    return 42;
}
