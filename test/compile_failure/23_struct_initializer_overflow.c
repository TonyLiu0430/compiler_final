// EXPECT-ERRORS: 1
// EXPECT: too many initializer elements
struct Pair {
    int first;
    int second;
};

Pair pair = {1, 2, 3};
