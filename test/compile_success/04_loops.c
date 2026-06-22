int main() {
    int result = 0;
    for (int i = 0; i < 5; i++) {
        if (i == 2)
            continue;
        result += i;
    }
    while (result < 20)
        result++;
    do {
        result--;
    } while (result > 19);
    return result;
}
