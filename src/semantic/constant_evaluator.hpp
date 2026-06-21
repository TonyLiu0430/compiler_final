#pragma once

#include <optional>
#include <string_view>

#include "base/constant_value.hpp"
#include "base/literal.hpp"
#include "parser/reflect_dispatch.hpp"

namespace c9ay::semantic {

class Constant_evaluator {
    using Result = std::optional<constant::Integer_value>;

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
        auto operand = evaluate(*expression.operand);
        if (!operand) return std::nullopt;
        return constant::evaluate_prefix(expression.op.raw, *operand);
    }

    Result operator()(const parser::Binary_expression &expression) {
        auto lhs = evaluate(*expression.lhs);
        if (!lhs) return std::nullopt;

        if (expression.op.raw == "&&" && lhs->value == 0) {
            return constant::Integer_value{0};
        }
        if (expression.op.raw == "||" && lhs->value != 0) {
            return constant::Integer_value{1};
        }

        auto rhs = evaluate(*expression.rhs);
        if (!rhs) return std::nullopt;
        return constant::evaluate_binary(
            expression.op.raw,
            *lhs,
            *rhs);
    }

    Result operator()(const parser::Conditional_expression &expression) {
        auto condition = evaluate(*expression.condition);
        if (!condition) return std::nullopt;
        return condition->value
            ? evaluate(*expression.true_expression)
            : evaluate(*expression.false_expression);
    }

    Result operator()(const parser::Cast_expression &expression) {
        return evaluate(*expression.operand);
    }

    Result operator()(const parser::Expression &) {
        return std::nullopt;
    }

    static Result evaluate(const parser::Expression &expression) {
        if (expression.error_occur) return std::nullopt;
        Constant_evaluator evaluator;
        return parser::reflect::dispatch<Result>(
            expression,
            evaluator);
    }
};

}  // namespace c9ay::semantic
