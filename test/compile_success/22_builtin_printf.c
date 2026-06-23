int main() {
    char *text = "value";
    int signed_value = -42;
    unsigned long long unsigned_value = 18446744073709551615ULL;

    printf(
        "%s: %d %u %c %%\n",
        text,
        signed_value,
        unsigned_value,
        '!');
    return 0;
}
