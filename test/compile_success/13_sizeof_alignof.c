struct Value {
    char tag;
    int number;
};

int bytes[sizeof(int) * 2];

int main() {
    Value value = {0, 42};
    return sizeof(value) + alignof(Value);
}
