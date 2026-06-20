#pragma once

#include <meta>
#include <memory>
#include <vector>

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

struct If_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_IF;

    static std::unique_ptr<Node> match(lexer::Lexer&) {
        return nullptr;
    }
};

struct While_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_WHILE;

    static std::unique_ptr<Node> match(lexer::Lexer&) {
        return nullptr;
    }
};

struct Program : Node {
    static std::unique_ptr<Node> match(lexer::Lexer&) {
        return nullptr;
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
            return nullptr;
        }
        cur.varible_name = token;
        return std::make_unique<Declvariable>(std::move(cur));
    }
};

consteval bool has_ll1_start(std::meta::info type) {
    for (auto member : std::meta::members_of(
             type, std::meta::access_context::current())) {
        if (std::meta::is_variable(member) &&
            std::meta::is_static_member(member) &&
            std::meta::has_identifier(member) &&
            std::meta::identifier_of(member) == "start") {
            return true;
        }
    }
    return false;
}

consteval auto ll1_statement_types() {
    std::vector<std::meta::info> result;

    for (auto member : std::meta::members_of(^^c9ay::parser, std::meta::access_context::current())) {
        if (std::meta::is_type(member) && std::meta::is_base_of_type(^^Statement, member) &&
            !std::meta::is_same_type(^^Statement, member) &&
            has_ll1_start(member)) {
            result.push_back(member);
        }
    }

    return std::define_static_array(result);
}

struct Statement_dispatch_result {
    bool recognized = false;
    std::unique_ptr<Node> node;
};

inline Statement_dispatch_result dispatch_statement(lexer::Lexer& lexer) {
    const lexer::Token token = lexer.peek_next();
    Statement_dispatch_result result;

    template for (constexpr auto type : ll1_statement_types()) {
        bool matches_type = false;

        template for (constexpr auto member :
                      std::define_static_array(std::meta::members_of(
                          type, std::meta::access_context::current()))) {
            if constexpr (std::meta::is_variable(member) &&
                          std::meta::is_static_member(member) &&
                          std::meta::has_identifier(member)) {
                if constexpr (std::meta::identifier_of(member) == "start") {
                    if (!result.recognized && token.type == [:member:]) {
                        result.recognized = true;
                        matches_type = true;
                    }
                }
            }
        }

        if (matches_type) {
            template for (constexpr auto member :
                          std::define_static_array(std::meta::members_of(
                              type, std::meta::access_context::current()))) {
                if constexpr (std::meta::is_function(member) &&
                              std::meta::has_identifier(member)) {
                    if constexpr (std::meta::identifier_of(member) == "match") {
                        result.node = [:member:](lexer);
                    }
                }
            }
        }
    }

    return result;
}

struct Block : Node {
    static std::unique_ptr<Node> match(lexer::Lexer& lexer) {
        Block cur;

        if (!lexer.next_token().match<lexer::token_type::PUNCTUATOR>('{')) {
            lexer.report_error("block expect '{'");
            return nullptr;
        }

        while (lexer.has_next()) {
            if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>('}')) {
                lexer.next_token();
                return std::make_unique<Block>(std::move(cur));
            }

            auto dispatched = dispatch_statement(lexer);
            if (!dispatched.recognized) {
                lexer.report_error("unexpected token in block");
                lexer.next_token();
                continue;
            }

            if (dispatched.node) {
                cur.children.push_back(std::move(dispatched.node));
            }
            else {
                // A recognized parser failed. Consume one token so an
                // incomplete parser cannot make this loop stall forever.
                lexer.next_token();
            }
        }

        lexer.report_error("block not close");
        return nullptr;
    }
};

}  // namespace c9ay::parser
