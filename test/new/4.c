#include <c9ay.h>
int main() {
    int value = 40;
    int* pointer = &value;
    *pointer += 2;
    print_int(*pointer);
}
