// EXPECT-ERRORS: 1
// EXPECT: dereference operand is not a pointer
int main() {
    int value = 1;
    return *value;
}
