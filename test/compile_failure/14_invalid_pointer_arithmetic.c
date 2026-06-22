// EXPECT-ERRORS: 1
// EXPECT: binary operator requires integer operands
int main() {
    int value = 0;
    int *pointer = &value;
    return pointer * 2;
}
