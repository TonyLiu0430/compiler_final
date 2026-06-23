struct Pair {
    int first;
    int second;
};

int sum(struct Pair *pair) {
    return pair->first + pair->second;
}

int main() {
    struct Pair value = {20, 22};
    return sum(&value);
}
