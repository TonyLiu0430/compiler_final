struct Pair {
    char tag;
    int value;
};

int values[sizeof(int) * 2];

int main() {
    int value = 1;
    return sizeof(value++) +
           sizeof(int *) +
           sizeof(int[3]) +
           alignof(Pair) +
           sizeof(Pair) +
           value;
}
