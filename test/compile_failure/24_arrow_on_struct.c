// EXPECT-ERRORS: 1
// EXPECT: '->' requires pointer to struct
struct Pair {
    int value;
};

int main() {
    Pair pair = {1};
    return pair->value;
}
