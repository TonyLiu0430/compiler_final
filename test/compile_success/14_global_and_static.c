static int hidden = 40;
int visible = 2;

static int read_hidden() {
    return hidden;
}

int main() {
    return read_hidden() + visible;
}
