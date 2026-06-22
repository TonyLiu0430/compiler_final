// EXPECT-ERRORS: 1
// EXPECT: duplicate struct member 'value'
struct Pair {
    int value;
    int value;
};
