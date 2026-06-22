int absolute(int value) {
    if (value < 0)
        return -value;
    else
        return value;
}

int main() {
    return absolute(-42);
}
