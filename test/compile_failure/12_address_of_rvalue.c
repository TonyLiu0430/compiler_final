// EXPECT-ERRORS: 1
// EXPECT: address-of operand is not an lvalue
int main() {
    int *pointer = &(1 + 2);
    return 0;
}
