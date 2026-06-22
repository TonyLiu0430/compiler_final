// EXPECT-ERRORS: 1
// EXPECT: wrong number of function arguments
int add(int left, int right) {
    return left + right;
}

int main() {
    return add(1);
}
