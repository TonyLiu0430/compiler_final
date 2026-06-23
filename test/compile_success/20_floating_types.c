double average(double left, double right) {
    return (left + right) / 2.0;
}

int main() {
    float small = 20.0f;
    double normal = 22.0;
    long double wide = 2.0L;
    return (int)(average(small, normal) * wide);
}
