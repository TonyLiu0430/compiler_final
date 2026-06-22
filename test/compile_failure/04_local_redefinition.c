// EXPECT-ERRORS: 1
// EXPECT: redefinition of 'value'
int main() {
    int value;
    int value;
    return 0;
}
