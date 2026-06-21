#pragma once

namespace c9ay::parser {

struct Node {
    bool error_occur = false;

    Node() = default;
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    Node(Node &&) = default;
    Node &operator=(Node &&) = default;
    virtual ~Node() = default;
};

}  // namespace c9ay::parser
