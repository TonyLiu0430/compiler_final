// EXPECT-ERRORS: 1
// EXPECT: duplicate case value
int main() {
    switch (1) {
        case 1:
            break;
        case 1:
            break;
    }
    return 0;
}
