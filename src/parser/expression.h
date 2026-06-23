#pragma once

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "lexer/lexer.hpp"
#include "parser/node.h"
#include "parser/type_specifier.hpp"
#include "parser/type_names.hpp"

namespace c9ay::parser {

struct Expression : Node {
    struct Panic_sync {
        static bool is_sync_token(lexer::Token tok) {
            if (tok.type == lexer::token_type::PUNCTUATOR &&
                (tok.raw == ";" || tok.raw == "}")) {
                return true;
            }
            if (tok.type == lexer::token_type::OPERATOR &&
                (tok.raw == "," || tok.raw == ")" || tok.raw == "]" ||
                 tok.raw == ":")) {
                return true;
            }
            return false;
        }
    };

    static std::unique_ptr<Expression> match(lexer::Lexer &lexer);
    static std::unique_ptr<Expression> parse(lexer::Lexer &lexer, int min_binding_power = 0);
    static std::unique_ptr<Expression> recover(lexer::Lexer &lexer);
};

struct Error_expression : Expression {
    Error_expression() {
        error_occur = true;
    }
};

struct Primary_expression : Expression {
    lexer::Token token;

    Primary_expression(lexer::Token _token) : token(_token) {}
};

struct Prefix_expression : Expression {
    lexer::Token op;
    std::unique_ptr<Expression> operand;

    Prefix_expression(lexer::Token _op, std::unique_ptr<Expression> _operand)
        : op(_op), operand(std::move(_operand)) {
        error_occur = operand->error_occur;
    }
};

struct Binary_expression : Expression {
    lexer::Token op;
    std::unique_ptr<Expression> lhs;
    std::unique_ptr<Expression> rhs;

    Binary_expression(
        lexer::Token _op,
        std::unique_ptr<Expression> _lhs,
        std::unique_ptr<Expression> _rhs)
        : op(_op), lhs(std::move(_lhs)), rhs(std::move(_rhs)) {
        error_occur = lhs->error_occur || rhs->error_occur;
    }
};

struct Postfix_expression : Expression {
    lexer::Token op;
    std::unique_ptr<Expression> operand;

    Postfix_expression(lexer::Token _op, std::unique_ptr<Expression> _operand)
        : op(_op), operand(std::move(_operand)) {
        error_occur = operand->error_occur;
    }
};

struct Call_expression : Expression {
    std::unique_ptr<Expression> callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    Call_expression(std::unique_ptr<Expression> _callee)
        : callee(std::move(_callee)) {
        error_occur = callee->error_occur;
    }

    void add_argument(std::unique_ptr<Expression> argument) {
        error_occur |= argument->error_occur;
        arguments.push_back(std::move(argument));
    }
};

struct Subscript_expression : Expression {
    std::unique_ptr<Expression> object;
    std::unique_ptr<Expression> index;

    Subscript_expression(
        std::unique_ptr<Expression> _object,
        std::unique_ptr<Expression> _index)
        : object(std::move(_object)), index(std::move(_index)) {
        error_occur = object->error_occur || index->error_occur;
    }
};

struct Member_expression : Expression {
    std::unique_ptr<Expression> object;
    lexer::Token op;
    lexer::Token member;

    Member_expression(
        std::unique_ptr<Expression> _object,
        lexer::Token _op,
        lexer::Token _member)
        : object(std::move(_object)), op(_op), member(_member) {
        error_occur = object->error_occur;
    }
};

struct Conditional_expression : Expression {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> true_expression;
    std::unique_ptr<Expression> false_expression;

    Conditional_expression(
        std::unique_ptr<Expression> _condition,
        std::unique_ptr<Expression> _true_expression,
        std::unique_ptr<Expression> _false_expression)
        : condition(std::move(_condition)),
          true_expression(std::move(_true_expression)),
          false_expression(std::move(_false_expression)) {
        error_occur = condition->error_occur ||
                      true_expression->error_occur ||
                      false_expression->error_occur;
    }
};

struct Abstract_declarator : Node {
    int pointer_depth = 0;
    std::vector<std::unique_ptr<Expression>> array_dimensions;

    static std::unique_ptr<Abstract_declarator> match(lexer::Lexer &lexer);
};

struct Type_name : Node {
    lexer::Token type;
    std::string type_name;
    bool is_const = false;
    std::unique_ptr<Abstract_declarator> declarator;

    Type_name(
        Parsed_type_specifier _type,
        bool _is_const)
        : type(_type.first),
          type_name(std::move(_type.name)),
          is_const(_is_const) {}

    static std::unique_ptr<Type_name> try_match(lexer::Lexer &lexer);
};

struct Cast_expression : Expression {
    std::unique_ptr<Type_name> type;
    std::unique_ptr<Expression> operand;

    Cast_expression(
        std::unique_ptr<Type_name> _type,
        std::unique_ptr<Expression> _operand)
        : type(std::move(_type)), operand(std::move(_operand)) {
        error_occur = operand->error_occur;
    }
};

struct Type_query_expression : Expression {
    lexer::Token op;
    std::unique_ptr<Type_name> type;
    std::unique_ptr<Expression> operand;

    Type_query_expression(
        lexer::Token _op,
        std::unique_ptr<Type_name> _type)
        : op(_op), type(std::move(_type)) {
        error_occur = type->error_occur;
    }

    Type_query_expression(
        lexer::Token _op,
        std::unique_ptr<Expression> _operand)
        : op(_op), operand(std::move(_operand)) {
        error_occur = operand->error_occur;
    }
};

inline bool expect_operator(lexer::Lexer &lexer, char ch, std::string_view message) {
    if (!lexer.has_next() ||
        !lexer.peek_next().match<lexer::token_type::OPERATOR>(ch)) {
        lexer.report_error(std::string(message));
        return false;
    }
    lexer.next_token();
    return true;
}

inline std::pair<int, int> infix_binding_power(std::string_view op) {
    if (op == ",") return {1, 2};
    if (op == "=" || op == "+=" || op == "-=" || op == "*=" ||
        op == "/=" || op == "%=" || op == "<<=" || op == ">>=" ||
        op == "&=" || op == "^=" || op == "|=") {
        return {2, 2};
    }
    if (op == "||") return {4, 5};
    if (op == "&&") return {5, 6};
    if (op == "|") return {6, 7};
    if (op == "^") return {7, 8};
    if (op == "&") return {8, 9};
    if (op == "==" || op == "!=") return {9, 10};
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return {10, 11};
    if (op == "<<" || op == ">>") return {11, 12};
    if (op == "+" || op == "-") return {12, 13};
    if (op == "*" || op == "/" || op == "%") return {13, 14};
    return {-1, -1};
}

inline bool is_prefix_operator(std::string_view op) {
    return op == "+" || op == "-" || op == "!" || op == "~" ||
           op == "++" || op == "--" || op == "*" || op == "&";
}

inline bool is_postfix_operator(std::string_view op) {
    return op == "++" || op == "--";
}

inline bool can_start_expression(lexer::Token token) {
    return token.match<lexer::token_type::IDENTIFIER>() ||
           token.match<lexer::token_type::K_SIZEOF>() ||
           token.match<lexer::token_type::K_ALIGNOF>() ||
           token.match<lexer::token_type::NUMBER>() ||
           token.match<lexer::token_type::STRING_CONSTANT>() ||
           token.match<lexer::token_type::CHAR_CONSTANT>() ||
           token.match<lexer::token_type::OPERATOR>('(') ||
           (token.match<lexer::token_type::OPERATOR>() &&
            is_prefix_operator(token.raw));
}

inline std::unique_ptr<Abstract_declarator> Abstract_declarator::match(
    lexer::Lexer &lexer) {
    auto cur = std::make_unique<Abstract_declarator>();

    while (lexer.has_next() &&
           lexer.peek_next().match<lexer::token_type::OPERATOR>('*')) {
        lexer.next_token();
        cur->pointer_depth++;
    }

    while (lexer.has_next() &&
           lexer.peek_next().match<lexer::token_type::OPERATOR>('[')) {
        lexer.next_token();

        if (lexer.has_next() &&
            lexer.peek_next().match<lexer::token_type::OPERATOR>(']')) {
            lexer.next_token();
            cur->array_dimensions.push_back(nullptr);
            continue;
        }

        auto dimension = Expression::parse(lexer, 2);
        if (!dimension) {
            dimension = Expression::recover(lexer);
        }
        cur->error_occur |= dimension->error_occur;
        cur->array_dimensions.push_back(std::move(dimension));

        if (!expect_operator(lexer, ']', "expect ']' after abstract array dimension")) {
            return nullptr;
        }
    }

    if (cur->pointer_depth == 0 && cur->array_dimensions.empty()) {
        return nullptr;
    }
    return cur;
}

inline std::unique_ptr<Type_name> Type_name::try_match(lexer::Lexer &lexer) {
    bool is_const = false;
    while (lexer.has_next() &&
           lexer.peek_next().match<lexer::token_type::K_CONST>()) {
        lexer.next_token();
        is_const = true;
    }

    auto type = match_type_specifier(lexer);
    if (!type) return nullptr;

    auto cur = std::make_unique<Type_name>(
        std::move(*type), is_const);

    while (lexer.has_next() &&
           lexer.peek_next().match<lexer::token_type::K_CONST>()) {
        lexer.next_token();
        cur->is_const = true;
    }

    cur->declarator = Abstract_declarator::match(lexer);
    if (cur->declarator) {
        cur->error_occur |= cur->declarator->error_occur;
    }
    return cur;
}

inline std::unique_ptr<Expression> Expression::parse(lexer::Lexer &lexer, int min_binding_power) {
    if (!lexer.has_next()) {
        lexer.report_error("expect expression");
        return nullptr;
    }

    if (Panic_sync::is_sync_token(lexer.peek_next())) {
        lexer.report_error("expect expression");
        return nullptr;
    }

    lexer::Token token = lexer.next_token();
    std::unique_ptr<Expression> lhs;

    if (token.match<lexer::token_type::K_SIZEOF>() ||
        token.match<lexer::token_type::K_ALIGNOF>()) {
        if (!expect_operator(
                lexer,
                '(',
                "expect '(' after type query operator")) {
            return nullptr;
        }

        lexer::Lexer probe = lexer;
        auto probe_type = Type_name::try_match(probe);
        bool has_type =
            probe_type &&
            probe.has_next() &&
            probe.peek_next().match<lexer::token_type::OPERATOR>(')');

        if (has_type) {
            auto type = Type_name::try_match(lexer);
            assert_c9ay(type != nullptr);
            assert_c9ay(expect_operator(
                lexer,
                ')',
                "expect ')' after type name"));
            lhs = std::make_unique<Type_query_expression>(
                token,
                std::move(type));
        }
        else {
            if (token.match<lexer::token_type::K_ALIGNOF>()) {
                lexer.report_error("alignof requires a type name");
                auto ignored = parse(lexer);
                if (!ignored) {
                    ignored = recover(lexer);
                }
                if (!expect_operator(
                        lexer,
                        ')',
                        "expect ')' after alignof operand")) {
                    return nullptr;
                }
                lhs = std::make_unique<Error_expression>();
            }
            else {
                auto operand = parse(lexer);
                if (!operand) {
                    operand = recover(lexer);
                }
                if (!expect_operator(
                        lexer,
                        ')',
                        "expect ')' after sizeof expression")) {
                    return nullptr;
                }
                lhs = std::make_unique<Type_query_expression>(
                    token,
                    std::move(operand));
            }
        }
    }
    else if (token.match<lexer::token_type::OPERATOR>('(')) {
        lexer::Lexer probe = lexer;
        auto probe_type = Type_name::try_match(probe);
        bool is_cast =
            probe_type &&
            probe.has_next() &&
            probe.peek_next().match<lexer::token_type::OPERATOR>(')');

        if (is_cast) {
            probe.next_token();
            is_cast =
                probe.has_next() &&
                can_start_expression(probe.peek_next());
        }

        if (is_cast) {
            auto cast_type = Type_name::try_match(lexer);
            assert_c9ay(cast_type != nullptr);
            assert_c9ay(expect_operator(lexer, ')', "expect ')' after type name"));
            auto operand = parse(lexer, 14);
            if (!operand) {
                operand = recover(lexer);
            }

            lhs = std::make_unique<Cast_expression>(
                std::move(cast_type), std::move(operand));
        }
        else {
            lhs = parse(lexer);
            if (!lhs) {
                lhs = recover(lexer);
            }
            if (!expect_operator(lexer, ')', "expect ')' after expression")) {
                return nullptr;
            }
        }
    }
    else if (token.match<lexer::token_type::IDENTIFIER>() ||
             token.match<lexer::token_type::NUMBER>() ||
             token.match<lexer::token_type::STRING_CONSTANT>() ||
             token.match<lexer::token_type::CHAR_CONSTANT>()) {
        lhs = std::make_unique<Primary_expression>(token);
    }
    else if (token.match<lexer::token_type::OPERATOR>() &&
             is_prefix_operator(token.raw)) {
        auto operand = parse(lexer, 14);
        if (!operand) {
            operand = recover(lexer);
        }

        lhs = std::make_unique<Prefix_expression>(
            token, std::move(operand));
    }
    else {
        lexer.report_error("expect expression");
        return nullptr;
    }

    while (lexer.has_next()) {
        lexer::Token next = lexer.peek_next();

        if (next.match<lexer::token_type::OPERATOR>('(')) {
            if (15 < min_binding_power) break;

            lexer.next_token();
            auto call = std::make_unique<Call_expression>(std::move(lhs));

            if (!lexer.has_next()) {
                lexer.report_error("expect ')' after arguments");
                return nullptr;
            }

            if (!lexer.peek_next().match<lexer::token_type::OPERATOR>(')')) {
                while (1) {
                    auto argument = parse(lexer, 2);
                    if (!argument) {
                        argument = recover(lexer);
                    }
                    call->add_argument(std::move(argument));

                    if (!lexer.has_next()) {
                        lexer.report_error("expect ')' after arguments");
                        return nullptr;
                    }
                    if (lexer.peek_next().match<
                            lexer::token_type::OPERATOR>(')')) {
                        break;
                    }
                    if (!expect_operator(lexer, ',', "expect ',' between arguments")) {
                        return nullptr;
                    }
                }
            }
            lexer.next_token();
            lhs = std::move(call);
            continue;
        }

        if (next.match<lexer::token_type::OPERATOR>('[')) {
            if (15 < min_binding_power) break;

            lexer.next_token();
            auto index = parse(lexer);
            if (!index) {
                index = recover(lexer);
            }
            if (!expect_operator(lexer, ']', "expect ']' after subscript")) {
                return nullptr;
            }

            lhs = std::make_unique<Subscript_expression>(
                std::move(lhs), std::move(index));
            continue;
        }

        if (next.match<lexer::token_type::OPERATOR>() &&
            is_postfix_operator(next.raw)) {
            if (15 < min_binding_power) break;

            lexer.next_token();
            lhs = std::make_unique<Postfix_expression>(
                next, std::move(lhs));
            continue;
        }

        if (next.type == lexer::token_type::OPERATOR &&
            (next.raw == "." || next.raw == "->")) {
            if (15 < min_binding_power) break;

            lexer.next_token();
            if (!lexer.has_next() ||
                !lexer.peek_next().match<lexer::token_type::IDENTIFIER>()) {
                lexer.report_error("expect member name");
                return nullptr;
            }

            lhs = std::make_unique<Member_expression>(
                std::move(lhs), next, lexer.next_token());
            continue;
        }

        if (next.match<lexer::token_type::OPERATOR>('?')) {
            if (3 < min_binding_power) break;

            lexer.next_token();
            auto true_expression = parse(lexer);
            if (!true_expression) {
                true_expression = recover(lexer);
            }
            if (!expect_operator(lexer, ':', "expect ':' in conditional expression")) {
                return nullptr;
            }
            auto false_expression = parse(lexer, 3);
            if (!false_expression) {
                false_expression = recover(lexer);
            }

            lhs = std::make_unique<Conditional_expression>(
                std::move(lhs),
                std::move(true_expression),
                std::move(false_expression));
            continue;
        }

        if (!next.match<lexer::token_type::OPERATOR>()) break;

        auto [left_binding_power, right_binding_power] =
            infix_binding_power(next.raw);
        if (left_binding_power < min_binding_power) break;
        if (left_binding_power < 0) break;

        lexer.next_token();
        auto rhs = parse(lexer, right_binding_power);
        if (!rhs) {
            rhs = recover(lexer);
        }

        lhs = std::make_unique<Binary_expression>(
            next, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

inline std::unique_ptr<Expression> Expression::match(lexer::Lexer &lexer) {
    auto expression = parse(lexer);
    if (expression) {
        return expression;
    }
    return recover(lexer);
}

inline std::unique_ptr<Expression> Expression::recover(lexer::Lexer &lexer) {
    lexer.panic_recovery<Expression>(false);
    return std::make_unique<Error_expression>();
}

}  // namespace c9ay::parser
