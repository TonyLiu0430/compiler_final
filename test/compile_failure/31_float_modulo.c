// EXPECT-ERRORS: 1
// EXPECT: binary operator requires compatible arithmetic operands
int main() {
    double value = 4.0 % 2.0;
    return 0;
}
