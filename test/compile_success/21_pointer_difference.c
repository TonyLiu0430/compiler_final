int main() {
    int values[4] = {1, 2, 3, 4};
    int *begin = &values[0];
    int *end = &values[3];
    long long distance = end - begin;
    return (int)distance;
}
