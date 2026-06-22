// EXPECT-ERRORS: 1
// EXPECT: multiple default labels in one switch
int main() {
    switch (1) {
        default:
            break;
        default:
            break;
    }
    return 0;
}
