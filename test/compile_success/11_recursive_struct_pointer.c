struct Node {
    int value;
    Node *next;
};

int main() {
    Node tail = {2, 0};
    Node head = {40, &tail};
    return head.value + head.next->value;
}
