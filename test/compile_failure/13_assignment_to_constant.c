// EXPECT-ERRORS: 1
// EXPECT: assignment target is not a modifiable lvalue
int main() {
    const int value = 1;
    value = 2;
    return value;
}
