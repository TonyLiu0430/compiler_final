// EXPECT-ERRORS: 1
// EXPECT: member access requires complete struct type
struct Pair {
    int value;
};

int main() {
    Pair pair = {1};
    Pair *pointer = &pair;
    return pointer.value;
}
