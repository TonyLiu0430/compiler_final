struct Pair {
    int first;
    int second;
};

int sum(Pair *pair) {
    return pair->first + pair->second;
}

int main() {
    Pair value = {20, 22};
    return sum(&value);
}
