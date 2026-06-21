#pragma once

#include <optional>
#include <string_view>

#include "base/constant_value.hpp"
#include "parser/expression.h"

namespace c9ay::semantic {

class Constant_evaluator {
    static std::optional<constant::Integer_value> parse_number(
        std::string_view raw) {
        long long value = 0;
        for (char ch : raw) {
            if (ch < '0' || ch > '9') return std::nullopt;
            value = value * 10 + (ch - '0');
        }
        return constant::Integer_value{value};
    }

    static std::optional<constant::Integer_value> parse_character(
        std::string_view raw) {
        if (raw.size() < 3 || raw.front() != '\'' || raw.back() != '\'') {
            return std::nullopt;
        }
        if (raw[1] != '\\') {
            if (raw.size() != 3) return std::nullopt;
            return constant::Integer_value{
                static_cast<unsigned char>(raw[1])
            };
        }
        if (raw.size() != 4) return std::nullopt;

        if (raw[2] == 'n') return constant::Integer_value{'\n'};
        if (raw[2] == 't') return constant::Integer_value{'\t'};
        if (raw[2] == 'f') return constant::Integer_value{'\f'};
        if (raw[2] == '\'') return constant::Integer_value{'\''};
        if (raw[2] == '"') return constant::Integer_value{'"'};
        if (raw[2] == '\\') return constant::Integer_value{'\\'};
        return std::nullopt;
    }

public:
    static std::optional<constant::Integer_value> evaluate(
        const parser::Expression &expression) {
        if (expression.error_occur) return std::nullopt;

        if (auto primary =
                dynamic_cast<const parser::Primary_expression *>(&expression)) {
            if (primary->token.type == lexer::token_type::NUMBER) {
                return parse_number(primary->token.raw);
            }
            if (primary->token.type == lexer::token_type::CHAR_CONSTANT) {
                return parse_character(primary->token.raw);
            }
            return std::nullopt;
        }

        if (auto prefix =
                dynamic_cast<const parser::Prefix_expression *>(&expression)) {
            auto operand = evaluate(*prefix->operand);
            if (!operand) return std::nullopt;
            return constant::evaluate_prefix(prefix->op.raw, *operand);
        }

        if (auto binary =
                dynamic_cast<const parser::Binary_expression *>(&expression)) {
            auto lhs = evaluate(*binary->lhs);
            if (!lhs) return std::nullopt;

            if (binary->op.raw == "&&" && lhs->value == 0) {
                return constant::Integer_value{0};
            }
            if (binary->op.raw == "||" && lhs->value != 0) {
                return constant::Integer_value{1};
            }

            auto rhs = evaluate(*binary->rhs);
            if (!rhs) return std::nullopt;
            return constant::evaluate_binary(
                binary->op.raw,
                *lhs,
                *rhs);
        }

        if (auto conditional =
                dynamic_cast<const parser::Conditional_expression *>(
                    &expression)) {
            auto condition = evaluate(*conditional->condition);
            if (!condition) return std::nullopt;
            return condition->value
                ? evaluate(*conditional->true_expression)
                : evaluate(*conditional->false_expression);
        }

        if (auto cast =
                dynamic_cast<const parser::Cast_expression *>(&expression)) {
            return evaluate(*cast->operand);
        }

        return std::nullopt;
    }
};

}  // namespace c9ay::semantic
