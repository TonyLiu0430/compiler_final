int classify(int value) {
    switch (value) {
        case 1:
            return 10;
        case 2:
        case 3:
            return 20;
        default:
            return 30;
    }
}

int main() {
    return classify(2);
}
