int calculate(int left, int right) {
    return left * 3 + right / 2 - 4 % 3;
}

int main() {
    return calculate(10, 8);
}
