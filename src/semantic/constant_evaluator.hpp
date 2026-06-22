#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "base/constant_value.hpp"
#include "base/literal.hpp"
#include "parser/reflect_dispatch.hpp"

namespace c9ay::semantic {

class Constant_evaluator {
    using Result = std::optional<constant::Integer_value>;
    using Type_query_resolver =
        std::function<Result(const parser::Type_query_expression &)>;

    Type_query_resolver type_query_resolver;

    static Result parse_number(std::string_view raw) {
        long long value = 0;
        for (char ch : raw) {
            if (ch < '0' || ch > '9') return std::nullopt;
            value = value * 10 + (ch - '0');
        }
        return constant::Integer_value{value};
    }

    static Result parse_character(std::string_view raw) {
        auto value = literal::decode_character(raw);
        if (!value) return std::nullopt;
        return constant::Integer_value{*value};
    }

public:
    Constant_evaluator(
        Type_query_resolver _type_query_resolver = {})
        : type_query_resolver(std::move(_type_query_resolver)) {}

    Result evaluate_expression(
        const parser::Expression &expression) {
        if (expression.error_occur) return std::nullopt;
        return parser::reflect::dispatch<Result>(
            expression,
            *this);
    }

    Result operator()(const parser::Primary_expression &expression) {
        if (expression.token.type == lexer::token_type::NUMBER) {
            return parse_number(expression.token.raw);
        }
        if (expression.token.type == lexer::token_type::CHAR_CONSTANT) {
            return parse_character(expression.token.raw);
        }
        return std::nullopt;
    }

    Result operator()(const parser::Prefix_expression &expression) {
        auto operand = evaluate_expression(*expression.operand);
        if (!operand) return std::nullopt;
        return constant::evaluate_prefix(expression.op.raw, *operand);
    }

    Result operator()(const parser::Binary_expression &expression) {
        auto lhs = evaluate_expression(*expression.lhs);
        if (!lhs) return std::nullopt;

        if (expression.op.raw == "&&" && lhs->value == 0) {
            return constant::Integer_value{0};
        }
        if (expression.op.raw == "||" && lhs->value != 0) {
            return constant::Integer_value{1};
        }

        auto rhs = evaluate_expression(*expression.rhs);
        if (!rhs) return std::nullopt;
        return constant::evaluate_binary(
            expression.op.raw,
            *lhs,
            *rhs);
    }

    Result operator()(const parser::Conditional_expression &expression) {
        auto condition = evaluate_expression(*expression.condition);
        if (!condition) return std::nullopt;
        return condition->value
            ? evaluate_expression(*expression.true_expression)
            : evaluate_expression(*expression.false_expression);
    }

    Result operator()(const parser::Cast_expression &expression) {
        return evaluate_expression(*expression.operand);
    }

    Result operator()(
        const parser::Type_query_expression &expression) {
        if (!type_query_resolver) return std::nullopt;
        return type_query_resolver(expression);
    }

    Result operator()(const parser::Expression &) {
        return std::nullopt;
    }

    static Result evaluate(const parser::Expression &expression) {
        if (expression.error_occur) return std::nullopt;
        Constant_evaluator evaluator;
        return evaluator.evaluate_expression(expression);
    }
};

}  // namespace c9ay::semantic
