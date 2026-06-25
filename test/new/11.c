#include <c9ay.h>

int add(int left, int right) {
    return left + right;
}

int main() {
    int first = ;
    int second = 10 + ;

    if () {
        println("unreachable");
    }

    int third = add(1, );
    int values[3] = {1, 2, 3};
    int fourth = values[];

    println("panic recovery should still reach here");
    return first + second + third + fourth;
}
