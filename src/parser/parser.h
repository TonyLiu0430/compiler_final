#pragma once

#include "lexer/lexer.hpp"

namespace c9ay::parser {

struct Node {
    std::vector<std::unique_ptr<Node>> children;
    static std::unique_ptr<Node> match(lexer::Lexer&);  // TEMP
};

std::unique_ptr<Node> make_node() {
    return std::make_unique<Node>();
}

// struct Match_result {
//     std::unique_ptr<Node> node;
//     bool error_occur = false;
//     Match_result(): node()
// };

// template <typename T>
// std::unique_ptr<Node> parse(lexer::Lexer& lexer) {
//     std::unique_ptr<Node> res;
//     lexer::Lexer cur_state = lexer;
//     res = T::match(cur_state);
//     if (res != nullptr) {
//         lexer.sync(cur_state);
//         return res;
//     }
// }

// template <typename T, typename... Ts>
//     requires(sizeof...(Ts) > 0)
// std::unique_ptr<Node> parse(lexer::Lexer& lexer) {
//     std::unique_ptr<Node> res = parse<T>(lexer);
//     if (res != nullptr) {
//         return res;
//     }
//     return parse<Ts...>(lexer);
// }

struct Expression : Node {
    static std::unique_ptr<Node> match(lexer::Lexer&) {
        return nullptr;
    }
};

struct Statement : Node {
    static std::unique_ptr<Node> match(lexer::Lexer&) {
        return nullptr;
    }
};

struct If_statement : Node {};
struct While_statement : Node {};

struct Program : Node {
    static std::unique_ptr<Node> match(lexer::Lexer&) {
        return nullptr;
    }
};

struct Block : Node {
    static std::unique_ptr<Node> match(lexer::Lexer& lexer) {
        Block cur;
        lexer::Token token = lexer.next_token();
        if (token.match<lexer::token_type::PUNCTUATOR>('{')) {
        }
        bool success = false;
        while (lexer.has_next()) {
            lexer::Token next_token = lexer.peek_next();
            if (next_token.match<lexer::token_type::K_IF>()) {
                cur.children.push_back(If_statement::match(lexer));
            }
            else if (next_token.match<lexer::token_type::K_WHILE>()) {
                cur.children.push_back(While_statement::match(lexer));
            }
            else if (next_token.match<lexer::token_type::PUNCTUATOR>('}')) {
                lexer.next_token();
                success = true;
                break;
            }
        }
        if (!success) {
            report_error("block not close");
        }
    };

}  // namespace c9ay::parser
