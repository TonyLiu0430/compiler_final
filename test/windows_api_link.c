int MessageBoxA(
    void *window,
    char *text,
    char *caption,
    int type);

int main() {
    return MessageBoxA(
        0,
        "Hello from c9ay",
        "Windows API",
        0);
}
