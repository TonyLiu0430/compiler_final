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
    struct Panic_sync {
        static bool is_sync_token(lexer::Token tok) {
            if (tok.match<lexer::token_type::PUNCTUATOR>('}') || tok.match<lexer::token_type::PUNCTUATOR>(';')) {
                return true;
            }
            return false;
        }
    };
};

struct If_statement : Statement {};
struct While_statement : Statement {};

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
            lexer.report_error("block not close");
            return nullptr;
        }
        return std::make_unique<Block>(cur);
    }
};

struct Declvariable : Statement {
    lexer::Token type;
    lexer::Token varible_name;

    static std::unique_ptr<Node> match(lexer::Lexer& lexer) {
        Declvariable cur;
        bool is_const = false;
        bool is_static = false;
        lexer::Token token = lexer.next_token();
        while (!token.match<lexer::token_type::IDENTIFIER>()) {
            if (token.match<lexer::token_type::K_CONST>()) {
                if (is_const) {
                    lexer.report_error("duplicate 'const' declaration specifier");
                }
                is_const = true;
            }
            else if (token.match<lexer::token_type::K_STATIC>()) {
                if (is_static) {
                    lexer.report_error("duplicate 'static' declaration specifier");
                }
                is_static = true;
            }
            token = lexer.next_token();
        }
        cur.type = token;
        token = lexer.next_token();
        if (!token.match<lexer::token_type::IDENTIFIER>()) {
            lexer.report_error("varible declaration expect a identifier");
            lexer.panic_recovery<Declvariable>();
            return;
        }
    };
};

}  // namespace c9ay::parser
