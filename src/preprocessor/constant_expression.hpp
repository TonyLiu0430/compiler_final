#pragma once

#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

#include "base/constant_value.hpp"
#include "lexer/scanner.hpp"

namespace c9ay::preprocessor {

class Constant_expression {
    std::vector<scanner::Token> tokens;
    int cnt = 0;

    bool has_next() {
        return cnt < static_cast<int>(tokens.size());
    }

    bool consume(std::string_view raw) {
        if (!has_next() || tokens[cnt].raw != raw) return false;
        cnt++;
        return true;
    }

    std::optional<constant::Integer_value> primary() {
        if (consume("(")) {
            auto value = logical_or();
            if (!value || !consume(")")) return std::nullopt;
            return value;
        }

        if (!has_next()) return std::nullopt;
        scanner::Token token = tokens[cnt++];

        if (token.type == scanner::token_type::IDENTIFIER) {
            return constant::Integer_value{0};
        }
        if (token.type != scanner::token_type::NUMBER) {
            return std::nullopt;
        }

        long long value = 0;
        for (char ch : token.raw) {
            if (ch < '0' || ch > '9') return std::nullopt;
            value = value * 10 + (ch - '0');
        }
        return constant::Integer_value{value};
    }

    std::optional<constant::Integer_value> unary() {
        if (has_next() &&
            (tokens[cnt].raw == "+" ||
             tokens[cnt].raw == "-" ||
             tokens[cnt].raw == "!" ||
             tokens[cnt].raw == "~")) {
            std::string_view op = tokens[cnt++].raw;
            auto operand = unary();
            if (!operand) return std::nullopt;
            return constant::evaluate_prefix(op, *operand);
        }
        return primary();
    }

    template <class Next>
    std::optional<constant::Integer_value> binary(
        Next next,
        std::initializer_list<std::string_view> operators) {
        auto lhs = (this->*next)();
        if (!lhs) return std::nullopt;

        while (has_next()) {
            std::string_view op = tokens[cnt].raw;
            bool matches = false;
            for (auto candidate : operators) {
                matches |= op == candidate;
            }
            if (!matches) break;

            cnt++;
            auto rhs = (this->*next)();
            if (!rhs) return std::nullopt;
            lhs = constant::evaluate_binary(op, *lhs, *rhs);
            if (!lhs) return std::nullopt;
        }
        return lhs;
    }

    std::optional<constant::Integer_value> multiplicative() {
        return binary(
            &Constant_expression::unary,
            {"*", "/", "%"});
    }

    std::optional<constant::Integer_value> additive() {
        return binary(
            &Constant_expression::multiplicative,
            {"+", "-"});
    }

    std::optional<constant::Integer_value> shift() {
        return binary(
            &Constant_expression::additive,
            {"<<", ">>"});
    }

    std::optional<constant::Integer_value> relational() {
        return binary(
            &Constant_expression::shift,
            {"<", "<=", ">", ">="});
    }

    std::optional<constant::Integer_value> equality() {
        return binary(
            &Constant_expression::relational,
            {"==", "!="});
    }

    std::optional<constant::Integer_value> bit_and() {
        return binary(&Constant_expression::equality, {"&"});
    }

    std::optional<constant::Integer_value> bit_xor() {
        return binary(&Constant_expression::bit_and, {"^"});
    }

    std::optional<constant::Integer_value> bit_or() {
        return binary(&Constant_expression::bit_xor, {"|"});
    }

    std::optional<constant::Integer_value> logical_and() {
        return binary(&Constant_expression::bit_or, {"&&"});
    }

    std::optional<constant::Integer_value> logical_or() {
        return binary(&Constant_expression::logical_and, {"||"});
    }

public:
    Constant_expression(std::string_view expression) {
        int cursor = 0;
        while (1) {
            auto token = scanner::next_token(expression, cursor);
            if (token.type == scanner::token_type::END) break;
            if (token.type == scanner::token_type::WHITESPACE ||
                token.type == scanner::token_type::NEWLINE ||
                token.type == scanner::token_type::COMMENT) {
                continue;
            }
            tokens.push_back(token);
        }
    }

    std::optional<constant::Integer_value> evaluate() {
        auto value = logical_or();
        if (!value || has_next()) return std::nullopt;
        return value;
    }
};

}  // namespace c9ay::preprocessor
