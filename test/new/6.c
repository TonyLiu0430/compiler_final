#include <c9ay.h>

int main() {
    int values[] = {1, 2, 3, 4, 5};
    int sum = 0;

    for (int i = 0; i < 5; i++) {
        sum = sum + values[i];
    }

    printf("test 6: array sum = %d\n", sum);
    return 0;
}
