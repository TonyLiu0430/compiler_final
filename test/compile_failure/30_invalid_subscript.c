// EXPECT-ERRORS: 1
// EXPECT: invalid array subscript
int main() {
    int value = 1;
    return value[0];
}
