#include <c9ay.h>

#define BASE 40
#define ADD(left, right) ((left) + (right))

int main() {
#if BASE == 40
    int value = ADD(BASE, 2);
#else
    int value = 0;
#endif

    printf("test 8: preprocessor value = %d\n", value);
    return 0;
}
