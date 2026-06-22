// EXPECT-ERRORS: 1
// EXPECT: sizeof requires a complete object type
int main() {
    return sizeof(void);
}
