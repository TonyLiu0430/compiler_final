// EXPECT-ERRORS: 1
// EXPECT: incompatible initializer
int main() {
    int value = 1;
    int *pointer = value;
    return 0;
}
