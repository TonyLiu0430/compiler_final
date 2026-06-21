#pragma once

#include <meta>
#include <memory>
#include <optional>
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

struct Switch_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_SWITCH;

    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;

    static std::unique_ptr<Switch_statement> match(lexer::Lexer &lexer);
};

struct Case_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_CASE;

    std::unique_ptr<Expression> value;
    std::unique_ptr<Statement> statement;

    static std::unique_ptr<Case_statement> match(lexer::Lexer &lexer);
};

struct Default_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_DEFAULT;

    std::unique_ptr<Statement> statement;

    static std::unique_ptr<Default_statement> match(lexer::Lexer &lexer);
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

struct For_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_FOR;

    std::unique_ptr<Statement> initializer;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> iteration;
    std::unique_ptr<Statement> body;

    static std::unique_ptr<For_statement> match(lexer::Lexer &lexer);
};

struct Return_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_RETURN;

    std::unique_ptr<Expression> expression;

    static std::unique_ptr<Return_statement> match(lexer::Lexer &lexer);
};

struct Break_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_BREAK;

    static std::unique_ptr<Break_statement> match(lexer::Lexer &lexer);
};

struct Continue_statement : Statement {
    static constexpr lexer::token_type start = lexer::token_type::K_CONTINUE;

    static std::unique_ptr<Continue_statement> match(lexer::Lexer &lexer);
};

struct Expression_statement : Statement {
    std::unique_ptr<Expression> expression;

    static std::unique_ptr<Expression_statement> match(lexer::Lexer &lexer);
};

struct Declaration_specifiers {
    lexer::Token type;
    bool is_const;
    bool is_static;
    bool is_typedef;
};

struct Parameter_declaration;

struct Declarator : Node {
    lexer::Token name;
    int pointer_depth = 0;
    std::vector<std::unique_ptr<Expression>> array_dimensions;
    bool is_function = false;
    std::vector<std::unique_ptr<Parameter_declaration>> parameters;

    Declarator(lexer::Token _name, int _pointer_depth)
        : name(_name), pointer_depth(_pointer_depth) {}

    static std::unique_ptr<Declarator> match(lexer::Lexer &lexer);
};

struct Parameter_declaration : Node {
    Declaration_specifiers specifiers;
    std::unique_ptr<Declarator> declarator;

    Parameter_declaration(Declaration_specifiers _specifiers)
        : specifiers(_specifiers) {}

    static std::unique_ptr<Parameter_declaration> match(lexer::Lexer &lexer);
};

struct Initializer : Node {
    static std::unique_ptr<Initializer> match(lexer::Lexer &lexer);
};

struct Expression_initializer : Initializer {
    std::unique_ptr<Expression> expression;

    Expression_initializer(std::unique_ptr<Expression> _expression)
        : expression(std::move(_expression)) {
        error_occur = expression->error_occur;
    }
};

struct Initializer_list : Initializer {
    std::vector<std::unique_ptr<Initializer>> elements;

    void add_element(std::unique_ptr<Initializer> element) {
        error_occur |= element->error_occur;
        elements.push_back(std::move(element));
    }
};

struct Init_declarator : Node {
    std::unique_ptr<Declarator> declarator;
    std::unique_ptr<Initializer> initializer;

    Init_declarator(std::unique_ptr<Declarator> _declarator)
        : declarator(std::move(_declarator)) {
        error_occur = declarator->error_occur;
    }

    void set_initializer(std::unique_ptr<Initializer> _initializer) {
        initializer = std::move(_initializer);
        error_occur |= initializer->error_occur;
    }
};

struct Declvariable : Statement {
    lexer::Token type;
    bool is_const;
    bool is_static;
    bool is_typedef;
    std::vector<std::unique_ptr<Init_declarator>> declarators;

    Declvariable(
        lexer::Token _type,
        bool _is_const,
        bool _is_static,
        bool _is_typedef)
        : type(_type),
          is_const(_is_const),
          is_static(_is_static),
          is_typedef(_is_typedef) {}

    void add_declarator(std::unique_ptr<Init_declarator> declarator) {
        error_occur |= declarator->error_occur;
        declarators.push_back(std::move(declarator));
    }

    static std::unique_ptr<Declvariable> match(lexer::Lexer &lexer);
    static std::unique_ptr<Declvariable> try_match(lexer::Lexer &lexer);
};

struct Block : Statement {
    std::vector<std::unique_ptr<Statement>> statements;

    static std::unique_ptr<Block> match(lexer::Lexer &lexer);
};

struct Function_definition : Node {
    Declaration_specifiers specifiers;
    std::unique_ptr<Declarator> declarator;
    std::unique_ptr<Block> body;

    Function_definition(Declaration_specifiers _specifiers)
        : specifiers(_specifiers) {}

    static std::unique_ptr<Function_definition> try_match(lexer::Lexer &lexer);
};

struct Program : Node {
    std::vector<std::unique_ptr<Node>> external_declarations;

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

inline std::optional<Declaration_specifiers> match_declaration_specifiers(
    lexer::Lexer &lexer) {
    bool is_const = false;
    bool is_static = false;
    bool is_typedef = false;

    while (lexer.has_next()) {
        lexer::Token token = lexer.peek_next();
        if (token.match<lexer::token_type::K_TYPEDEF>()) {
            is_typedef = true;
        }
        else if (token.match<lexer::token_type::K_CONST>()) {
            is_const = true;
        }
        else if (token.match<lexer::token_type::K_STATIC>()) {
            is_static = true;
        }
        else {
            break;
        }
        lexer.next_token();
    }

    if (!lexer.has_next() ||
        !lexer.peek_next().match<lexer::token_type::IDENTIFIER>()) {
        return std::nullopt;
    }

    return Declaration_specifiers{
        lexer.next_token(),
        is_const,
        is_static,
        is_typedef
    };
}

inline bool can_start_declaration(lexer::Lexer lexer) {
    return match_declaration_specifiers(lexer).has_value();
}

inline bool can_start_declarator(lexer::Lexer lexer) {
    while (lexer.has_next() &&
           lexer.peek_next().raw.size() == 1 &&
           lexer.peek_next().match<lexer::token_type::OPERATOR>('*')) {
        lexer.next_token();
    }
    return lexer.has_next() &&
           lexer.peek_next().match<lexer::token_type::IDENTIFIER>();
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

inline std::unique_ptr<Switch_statement> Switch_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Switch_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_SWITCH>());

    if (!expect_operator(lexer, '(', "expect '(' after switch")) return nullptr;
    cur->condition = Expression::match(lexer);
    cur->error_occur |= cur->condition->error_occur;
    if (!expect_operator(lexer, ')', "expect ')' after switch condition")) return nullptr;

    cur->body = Statement::match(lexer);
    if (!cur->body) return nullptr;
    cur->error_occur |= cur->body->error_occur;
    return cur;
}

inline std::unique_ptr<Case_statement> Case_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Case_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_CASE>());

    cur->value = Expression::match(lexer);
    cur->error_occur |= cur->value->error_occur;
    if (!expect_operator(lexer, ':', "expect ':' after case expression")) {
        return nullptr;
    }

    cur->statement = Statement::match(lexer);
    if (!cur->statement) return nullptr;
    cur->error_occur |= cur->statement->error_occur;
    return cur;
}

inline std::unique_ptr<Default_statement> Default_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Default_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_DEFAULT>());

    if (!expect_operator(lexer, ':', "expect ':' after default")) {
        return nullptr;
    }

    cur->statement = Statement::match(lexer);
    if (!cur->statement) return nullptr;
    cur->error_occur |= cur->statement->error_occur;
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

inline std::unique_ptr<For_statement> For_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<For_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_FOR>());

    if (!expect_operator(lexer, '(', "expect '(' after for")) return nullptr;

    if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>(';')) {
        lexer.next_token();
    }
    else if (auto declaration = Declvariable::try_match(lexer)) {
        cur->initializer = std::move(declaration);
    }
    else {
        auto initializer = std::make_unique<Expression_statement>();
        initializer->expression = Expression::match(lexer);
        initializer->error_occur = initializer->expression->error_occur;
        if (!expect_punctuarter(lexer, ';', "expect ';' after for initializer")) {
            return nullptr;
        }
        cur->initializer = std::move(initializer);
    }

    if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>(';')) {
        lexer.next_token();
    }
    else {
        cur->condition = Expression::match(lexer);
        cur->error_occur |= cur->condition->error_occur;
        if (!expect_punctuarter(lexer, ';', "expect ';' after for condition")) {
            return nullptr;
        }
    }

    if (lexer.peek_next().raw.size() == 1 &&
        lexer.peek_next().match<lexer::token_type::OPERATOR>(')')) {
        lexer.next_token();
    }
    else {
        cur->iteration = Expression::match(lexer);
        cur->error_occur |= cur->iteration->error_occur;
        if (!expect_operator(lexer, ')', "expect ')' after for iteration")) {
            return nullptr;
        }
    }

    cur->body = Statement::match(lexer);
    if (!cur->body) return nullptr;

    if (cur->initializer) {
        cur->error_occur |= cur->initializer->error_occur;
    }
    cur->error_occur |= cur->body->error_occur;
    return cur;
}

inline std::unique_ptr<Return_statement> Return_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Return_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_RETURN>());

    if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>(';')) {
        lexer.next_token();
        return cur;
    }

    cur->expression = Expression::match(lexer);
    cur->error_occur = cur->expression->error_occur;
    if (!expect_punctuarter(lexer, ';', "expect ';' after return")) {
        return nullptr;
    }
    return cur;
}

inline std::unique_ptr<Break_statement> Break_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Break_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_BREAK>());
    if (!expect_punctuarter(lexer, ';', "expect ';' after break")) {
        return nullptr;
    }
    return cur;
}

inline std::unique_ptr<Continue_statement> Continue_statement::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Continue_statement>();
    assert_c9ay(lexer.next_token().match<lexer::token_type::K_CONTINUE>());
    if (!expect_punctuarter(lexer, ';', "expect ';' after continue")) {
        return nullptr;
    }
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

inline std::unique_ptr<Declarator> Declarator::match(lexer::Lexer &lexer) {
    int pointer_depth = 0;
    while (lexer.has_next() &&
           lexer.peek_next().raw.size() == 1 &&
           lexer.peek_next().match<lexer::token_type::OPERATOR>('*')) {
        lexer.next_token();
        pointer_depth++;
    }

    if (!lexer.has_next() ||
        !lexer.peek_next().match<lexer::token_type::IDENTIFIER>()) {
        lexer.report_error("declarator expect a identifier");
        return nullptr;
    }

    auto cur = std::make_unique<Declarator>(
        lexer.next_token(), pointer_depth);

    while (lexer.has_next()) {
        if (lexer.peek_next().raw.size() == 1 &&
            lexer.peek_next().match<lexer::token_type::OPERATOR>('[')) {
            lexer.next_token();

            if (lexer.peek_next().raw.size() == 1 &&
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

            if (!expect_operator(lexer, ']', "expect ']' after array dimension")) {
                return nullptr;
            }
            continue;
        }

        if (lexer.peek_next().raw.size() == 1 &&
            lexer.peek_next().match<lexer::token_type::OPERATOR>('(')) {
            lexer.next_token();
            cur->is_function = true;

            if (lexer.peek_next().raw.size() == 1 &&
                lexer.peek_next().match<lexer::token_type::OPERATOR>(')')) {
                lexer.next_token();
                continue;
            }

            while (1) {
                auto parameter = Parameter_declaration::match(lexer);
                if (!parameter) return nullptr;
                cur->error_occur |= parameter->error_occur;
                cur->parameters.push_back(std::move(parameter));

                if (lexer.peek_next().raw.size() == 1 &&
                    lexer.peek_next().match<lexer::token_type::OPERATOR>(')')) {
                    lexer.next_token();
                    break;
                }
                if (!expect_operator(lexer, ',', "expect ',' between parameters")) {
                    return nullptr;
                }
            }
            continue;
        }

        break;
    }

    return cur;
}

inline std::unique_ptr<Parameter_declaration> Parameter_declaration::match(
    lexer::Lexer &lexer) {
    auto specifiers = match_declaration_specifiers(lexer);
    if (!specifiers) {
        lexer.report_error("parameter expect declaration specifiers");
        return nullptr;
    }

    auto cur = std::make_unique<Parameter_declaration>(*specifiers);

    if (lexer.peek_next().raw.size() == 1 &&
        (lexer.peek_next().match<lexer::token_type::OPERATOR>(')') ||
         lexer.peek_next().match<lexer::token_type::OPERATOR>(','))) {
        return cur;
    }

    cur->declarator = Declarator::match(lexer);
    if (!cur->declarator) return nullptr;
    cur->error_occur |= cur->declarator->error_occur;
    return cur;
}

inline std::unique_ptr<Initializer> Initializer::match(lexer::Lexer &lexer) {
    if (lexer.has_next() &&
        lexer.peek_next().match<lexer::token_type::PUNCTUATOR>('{')) {
        lexer.next_token();
        auto list = std::make_unique<Initializer_list>();

        if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>('}')) {
            lexer.next_token();
            return list;
        }

        while (1) {
            auto element = Initializer::match(lexer);
            if (!element) return nullptr;
            list->add_element(std::move(element));

            if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>('}')) {
                lexer.next_token();
                break;
            }

            if (!expect_operator(lexer, ',', "expect ',' between initializer elements")) {
                return nullptr;
            }

            if (lexer.peek_next().match<lexer::token_type::PUNCTUATOR>('}')) {
                lexer.next_token();
                break;
            }
        }

        return list;
    }

    auto expression = Expression::parse(lexer, 2);
    if (!expression) {
        expression = Expression::recover(lexer);
    }
    return std::make_unique<Expression_initializer>(
        std::move(expression));
}

inline std::unique_ptr<Declvariable> Declvariable::try_match(lexer::Lexer &lexer) {
    lexer::Lexer probe = lexer;
    if (!match_declaration_specifiers(probe)) return nullptr;
    if (!can_start_declarator(probe)) return nullptr;

    lexer::Lexer cur_state = lexer;
    auto result = match(cur_state);
    if (result) {
        lexer.sync(cur_state);
    }
    return result;
}

inline std::unique_ptr<Declvariable> Declvariable::match(lexer::Lexer &lexer) {
    auto specifiers = match_declaration_specifiers(lexer);
    if (!specifiers) return nullptr;

    auto cur = std::make_unique<Declvariable>(
        specifiers->type,
        specifiers->is_const,
        specifiers->is_static,
        specifiers->is_typedef);

    while (1) {
        auto declarator = Declarator::match(lexer);
        if (!declarator) return nullptr;

        auto init_declarator = std::make_unique<Init_declarator>(
            std::move(declarator));

        if (lexer.has_next() &&
            lexer.peek_next().raw.size() == 1 &&
            lexer.peek_next().match<lexer::token_type::OPERATOR>('=')) {
            lexer.next_token();
            auto initializer = Initializer::match(lexer);
            if (!initializer) return nullptr;
            init_declarator->set_initializer(std::move(initializer));
        }

        cur->add_declarator(std::move(init_declarator));

        if (!lexer.has_next() ||
            lexer.peek_next().raw.size() != 1 ||
            !lexer.peek_next().match<lexer::token_type::OPERATOR>(',')) {
            break;
        }
        lexer.next_token();
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

inline std::unique_ptr<Function_definition> Function_definition::try_match(
    lexer::Lexer &lexer) {
    lexer::Lexer probe = lexer;
    auto probe_specifiers = match_declaration_specifiers(probe);
    if (!probe_specifiers || !can_start_declarator(probe)) {
        return nullptr;
    }

    auto probe_declarator = Declarator::match(probe);
    if (!probe_declarator ||
        !probe_declarator->is_function ||
        !probe.has_next() ||
        !probe.peek_next().match<lexer::token_type::PUNCTUATOR>('{')) {
        return nullptr;
    }

    auto specifiers = match_declaration_specifiers(lexer);
    if (!specifiers) return nullptr;

    auto cur = std::make_unique<Function_definition>(*specifiers);
    cur->declarator = Declarator::match(lexer);
    if (!cur->declarator || !cur->declarator->is_function) {
        return nullptr;
    }

    cur->body = Block::match(lexer);
    if (!cur->body) return nullptr;
    cur->error_occur =
        cur->declarator->error_occur || cur->body->error_occur;
    return cur;
}

inline std::unique_ptr<Program> Program::match(lexer::Lexer &lexer) {
    auto cur = std::make_unique<Program>();
    while (lexer.has_next()) {
        if (auto function = Function_definition::try_match(lexer)) {
            cur->error_occur |= function->error_occur;
            cur->external_declarations.push_back(std::move(function));
            continue;
        }

        if (auto declaration = Declvariable::try_match(lexer)) {
            cur->error_occur |= declaration->error_occur;
            cur->external_declarations.push_back(std::move(declaration));
            continue;
        }

        lexer.report_error("expect external declaration");
        if (lexer.has_next()) {
            lexer.panic_recovery<Statement>();
        }
        cur->error_occur = true;
    }
    return cur;
}

}  // namespace c9ay::parser
