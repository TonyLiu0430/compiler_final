// EXPECT-ERRORS: 1
// EXPECT: alignof requires a type name
int main() {
    int value = 1;
    return alignof(value);
}
