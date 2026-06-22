// EXPECT-ERRORS: 1
// EXPECT: has no member 'missing'
struct Pair {
    int value;
};

int main() {
    Pair pair = {1};
    return pair.missing;
}
