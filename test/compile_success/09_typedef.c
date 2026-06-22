typedef int Integer;
typedef Integer *IntegerPointer;

int main() {
    Integer value = 42;
    IntegerPointer pointer = &value;
    return *pointer;
}
