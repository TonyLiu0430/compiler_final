struct Node {
    int value;
    Node *next;
};

int main() {
    Node tail = {1, 0};
    Node head = {41, &tail};
    return head.value + head.next->value;
}
