int add_one(int value) {
    return value + 1;
}

int main() {
    int (*function)(int value) = add_one;
    return function(41);
}
