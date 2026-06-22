int main() {
    int value = 40;
    int *pointer = &value;
    *pointer += 2;
    return *pointer;
}
