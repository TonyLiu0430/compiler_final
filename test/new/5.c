#include <c9ay.h>

struct Point {
    int x;
    int y;
};

int main() {
    struct Point p;
    p.x = 3;
    p.y = 4;

    struct Point *ptr = &p;
    printf(
        "test 5: point = (%d, %d), sum = %d\n",
        ptr->x,
        ptr->y,
        ptr->x + ptr->y);
    return 0;
}
