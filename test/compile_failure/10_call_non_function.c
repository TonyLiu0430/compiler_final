// EXPECT-ERRORS: 1
// EXPECT: called expression is not a function
int main() {
    int value = 1;
    return value();
}
