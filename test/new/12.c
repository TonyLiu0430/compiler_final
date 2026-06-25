#include <c9ay.h>

struct Item {
    int id;
    int price;
    int count;
};

int item_total(struct Item *item) {
    return item->price * item->count;
}

int clamp_discount(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 30) {
        return 30;
    }
    return value;
}

int main() {
    struct Item items[4] = {
        {1, 120, 2},
        {2, 80, 5},
        {3, 40, 10},
        {4, 300, 1},
    };

    int subtotal = 0;
    int expensive_count = 0;

    for (int i = 0; i < 4; i++) {
        int total = item_total(&items[i]);
        subtotal = subtotal + total;

        if (total >= 300) {
            expensive_count++;
        }
    }

    int discount_percent = clamp_discount(expensive_count * 8);
    int discount = subtotal * discount_percent / 100;
    int final_total = subtotal - discount;

    printf("subtotal = %d\n", subtotal);
    printf("discount = %d%%\n", discount_percent);
    printf("final = %d\n", final_total);

    return 0;
}
