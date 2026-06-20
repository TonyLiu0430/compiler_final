#pragma once

#include <meta>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "lexer/lexer.hpp"

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

struct Cast_expression : Expression {
    lexer::Token type;
    std::unique_ptr<Expression> operand;

    Cast_expression(lexer::Token _type, std::unique_ptr<Expression> _operand)
        : type(_type), operand(std::move(_operand)) {
        error_occur = operand->error_occur;
    }
};

struct Statement : Node {
    static std::unique_ptr<Statement> match(lexer::Lexer &lexer);

    struct Panic_sync {
        static bool is_sync_token(lexer::Token tok) {
            if (tok.match<lexer::token_type::PUNCTUATOR>('}') ||
                tok.match<lexer::token_type::PUNCTUATOR>(';')) {
                return true;
            }
            return false;
        }
    };
};

struct If_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_IF;

    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> then_statement;
    std::unique_ptr<Statement> else_statement;

    static std::unique_ptr<If_statement> match(lexer::Lexer &lexer);
};

struct While_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_WHILE;

    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;

    static std::unique_ptr<While_statement> match(lexer::Lexer &lexer);
};

struct Do_while_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_DO;

    std::unique_ptr<Statement> body;
    std::unique_ptr<Expression> condition;

    static std::unique_ptr<Do_while_statement> match(lexer::Lexer &lexer);
};

struct Expression_statement : Statement {
    std::unique_ptr<Expression> expression;

    static std::unique_ptr<Expression_statement> match(lexer::Lexer &lexer);
};

struct Declvariable : Statement {
    lexer::Token type;
    lexer::Token varible_name;
    bool is_const;
    bool is_static;
    std::unique_ptr<Expression> initializer;

    Declvariable(
        lexer::Token _type,
        lexer::Token _varible_name,
        bool _is_const,
        bool _is_static)
        : type(_type),
          varible_name(_varible_name),
          is_const(_is_const),
          is_static(_is_static) {}

    static std::unique_ptr<Declvariable> match(lexer::Lexer &lexer);
    static std::unique_ptr<Declvariable> try_match(lexer::Lexer &lexer);
};

struct Block : Statement {
    std::vector<std::unique_ptr<Statement>> statements;

    static std::unique_ptr<Block> match(lexer::Lexer &lexer);
};

struct Program : Node {
    std::vector<std::unique_ptr<Statement>> statements;

    static std::unique_ptr<Program> match(lexer::Lexer &lexer);
};

inline bool expect_operator(lexer::Lexer &lexer, char ch, std::string_view message) {
    if (!lexer.has_next() ||
        lexer.peek_next().raw.size() != 1 ||
        !lexer.peek_next().match<lexer::token_type::OPERATOR>(ch)) {
        lexer.report_error(std::string(message));
        return false;
    }
    lexer.next_token();
    return true;
}

inline bool expect_punctuarter(lexer::Lexer &lexer, char ch, std::string_view message) {
    if (!lexer.has_next() ||
        !lexer.peek_next().match<lexer::token_type::PUNCTUATOR>(ch)) {
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
           token.match<lexer::token_type::NUMBER>() ||
           token.match<lexer::token_type::STRING_CONSTANT>() ||
           token.match<lexer::token_type::CHAR_CONSTANT>() ||
           (token.raw.size() == 1 &&
            token.match<lexer::token_type::OPERATOR>('(')) ||
           (token.match<lexer::token_type::OPERATOR>() &&
            is_prefix_operator(token.raw));
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

    if (token.raw.size() == 1 &&
        token.match<lexer::token_type::OPERATOR>('(')) {
        if (lexer.has_next(2) &&
            lexer.peek_next().match<lexer::token_type::IDENTIFIER>() &&
            lexer.peek_next(1).raw.size() == 1 &&
            lexer.peek_next(1).match<lexer::token_type::OPERATOR>(')') &&
            can_start_expression(lexer.peek_next(2))) {
            lexer::Token cast_type = lexer.next_token();
            lexer.next_token();
            auto operand = parse(lexer, 14);
            if (!operand) {
                operand = recover(lexer);
            }

            lhs = std::make_unique<Cast_expression>(
                cast_type, std::move(operand));
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

        if (next.raw.size() == 1 &&
            next.match<lexer::token_type::OPERATOR>('(')) {
            if (15 < min_binding_power) break;

            lexer.next_token();
            auto call = std::make_unique<Call_expression>(std::move(lhs));

            if (!lexer.has_next()) {
                lexer.report_error("expect ')' after arguments");
                return nullptr;
            }

            if (lexer.peek_next().raw.size() != 1 ||
                !lexer.peek_next().match<lexer::token_type::OPERATOR>(')')) {
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
                    if (lexer.peek_next().raw.size() == 1 &&
                        lexer.peek_next().match<lexer::token_type::OPERATOR>(')')) {
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

        if (next.raw.size() == 1 &&
            next.match<lexer::token_type::OPERATOR>('[')) {
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

        if (next.raw.size() == 1 &&
            next.match<lexer::token_type::OPERATOR>('?')) {
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

    for (auto member : std::meta::members_of(
             ^^c9ay::parser, std::meta::access_context::current())) {
        if (std::meta::is_type(member) &&
            std::meta::is_base_of_type(^^Statement, member) &&
            !std::meta::is_same_type(^^Statement, member) &&
            has_ll1_start(member)) {
            result.push_back(member);
        }
    }

    return std::define_static_array(result);
}

struct Statement_dispatch_result {
    bool recognized = false;
    std::unique_ptr<Statement> statement;
};

inline Statement_dispatch_result dispatch_ll1_statement(lexer::Lexer &lexer) {
    lexer::Token token = lexer.peek_next();
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
                        result.statement = [:member:](lexer);
                    }
                }
            }
        }
    }

    return result;
}

inline std::unique_ptr<Statement> Statement::match(lexer::Lexer &lexer) {
    if (!lexer.has_next()) return nullptr;

    if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>('{')) {
        return Block::match(lexer);
    }

    auto ll1 = dispatch_ll1_statement(lexer);
    if (ll1.recognized) {
        return std::move(ll1.statement);
    }

    if (auto declaration = Declvariable::try_match(lexer)) {
        return declaration;
    }

    return Expression_statement::match(lexer);
}

inline std::unique_ptr<If_statement> If_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<If_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_IF>());

    if (!expect_operator(lexer, '(', "expect '(' after if")) return nullptr;
    cur->condition = Expression::match(lexer);
    if (!cur->condition) return nullptr;
    cur->error_occur |= cur->condition->error_occur;
    if (!expect_operator(lexer, ')', "expect ')' after if condition")) return nullptr;

    cur->then_statement = Statement::match(lexer);
    if (!cur->then_statement) return nullptr;
    cur->error_occur |= cur->then_statement->error_occur;

    if (lexer.has_next() &&
        lexer.peek_next().match<lexer::token_type::K_ELSE>()) {
        lexer.next_token();
        cur->else_statement = Statement::match(lexer);
        if (!cur->else_statement) return nullptr;
        cur->error_occur |= cur->else_statement->error_occur;
    }

    return cur;
}

inline std::unique_ptr<While_statement> While_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<While_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_WHILE>());

    if (!expect_operator(lexer, '(', "expect '(' after while")) return nullptr;
    cur->condition = Expression::match(lexer);
    if (!cur->condition) return nullptr;
    cur->error_occur |= cur->condition->error_occur;
    if (!expect_operator(lexer, ')', "expect ')' after while condition")) return nullptr;

    cur->body = Statement::match(lexer);
    if (!cur->body) return nullptr;
    cur->error_occur |= cur->body->error_occur;
    return cur;
}

inline std::unique_ptr<Do_while_statement> Do_while_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Do_while_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_DO>());

    cur->body = Statement::match(lexer);
    if (!cur->body) return nullptr;
    cur->error_occur |= cur->body->error_occur;

    if (!lexer.has_next() ||
        !lexer.next_token().match<lexer::token_type::K_WHILE>()) {
        lexer.report_error("expect while after do statement");
        return nullptr;
    }
    if (!expect_operator(lexer, '(', "expect '(' after while")) return nullptr;
    cur->condition = Expression::match(lexer);
    if (!cur->condition) return nullptr;
    cur->error_occur |= cur->condition->error_occur;
    if (!expect_operator(lexer, ')', "expect ')' after do while condition")) return nullptr;
    if (!expect_punctuarter(lexer, ';', "expect ';' after do while")) return nullptr;
    return cur;
}

inline std::unique_ptr<Expression_statement> Expression_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Expression_statement>();

    if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>(';')) {
        lexer.next_token();
        return cur;
    }

    cur->expression = Expression::match(lexer);
    if (!cur->expression) return nullptr;
    cur->error_occur = cur->expression->error_occur;
    if (!expect_punctuarter(lexer, ';', "expect ';' after expression")) return nullptr;
    return cur;
}

inline std::unique_ptr<Declvariable> Declvariable::try_match(lexer::Lexer &lexer) {
    lexer::Lexer probe = lexer;

    while (probe.has_next() &&
           (probe.peek_next().match<lexer::token_type::K_CONST>() ||
            probe.peek_next().match<lexer::token_type::K_STATIC>())) {
        probe.next_token();
    }

    if (!probe.has_next() ||
        !probe.next_token().match<lexer::token_type::IDENTIFIER>()) {
        return nullptr;
    }
    if (!probe.has_next() ||
        !probe.peek_next().match<lexer::token_type::IDENTIFIER>()) {
        return nullptr;
    }

    lexer::Lexer cur_state = lexer;
    auto result = match(cur_state);
    if (result) {
        lexer.sync(cur_state);
    }
    return result;
}

inline std::unique_ptr<Declvariable> Declvariable::match(lexer::Lexer &lexer) {
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
        else {
            return nullptr;
        }
        token = lexer.next_token();
    }

    lexer::Token type = token;
    lexer::Token varible_name = lexer.next_token();
    if (!varible_name.match<lexer::token_type::IDENTIFIER>()) {
        lexer.report_error("varible declaration expect a identifier");
        return nullptr;
    }

    auto cur = std::make_unique<Declvariable>(
        type, varible_name, is_const, is_static);

    if (lexer.has_next() &&
        lexer.peek_next().raw.size() == 1 &&
        lexer.peek_next().match<lexer::token_type::OPERATOR>('=')) {
        lexer.next_token();
        cur->initializer = Expression::match(lexer);
        if (!cur->initializer) return nullptr;
        cur->error_occur |= cur->initializer->error_occur;
    }

    if (!expect_punctuarter(lexer, ';', "expect ';' after varible declaration")) {
        return nullptr;
    }
    return cur;
}

inline std::unique_ptr<Block> Block::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Block>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::PUNCTUATOR>('{'));

    while (lexer.has_next()) {
        if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>('}')) {
            lexer.next_token();
            return cur;
        }

        auto statement = Statement::match(lexer);
        if (!statement) {
            lexer.panic_recovery<Statement>();
            continue;
        }
        cur->error_occur |= statement->error_occur;
        cur->statements.push_back(std::move(statement));
    }

    lexer.report_error("block not close");
    return nullptr;
}

inline std::unique_ptr<Program> Program::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Program>();
    while (lexer.has_next()) {
        auto statement = Statement::match(lexer);
        if (!statement) {
            lexer.panic_recovery<Statement>();
            continue;
        }
        cur->error_occur |= statement->error_occur;
        cur->statements.push_back(std::move(statement));
    }
    return cur;
}

}  // namespace c9ay::parser
