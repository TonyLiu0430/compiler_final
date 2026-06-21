#pragma once

#include <optional>
#include <string_view>

namespace c9ay::constant {

struct Integer_value {
    long long value = 0;
};

inline std::optional<Integer_value> evaluate_prefix(
    std::string_view op,
    Integer_value operand) {
    if (op == "+") return operand;
    if (op == "-") return Integer_value{-operand.value};
    if (op == "!") return Integer_value{!operand.value};
    if (op == "~") return Integer_value{~operand.value};
    return std::nullopt;
}

inline std::optional<Integer_value> evaluate_binary(
    std::string_view op,
    Integer_value lhs,
    Integer_value rhs) {
    if (op == "+") return Integer_value{lhs.value + rhs.value};
    if (op == "-") return Integer_value{lhs.value - rhs.value};
    if (op == "*") return Integer_value{lhs.value * rhs.value};
    if (op == "/") {
        if (rhs.value == 0) return std::nullopt;
        return Integer_value{lhs.value / rhs.value};
    }
    if (op == "%") {
        if (rhs.value == 0) return std::nullopt;
        return Integer_value{lhs.value % rhs.value};
    }
    if (op == "<<") {
        if (rhs.value < 0 || rhs.value >= 64) return std::nullopt;
        return Integer_value{lhs.value << rhs.value};
    }
    if (op == ">>") {
        if (rhs.value < 0 || rhs.value >= 64) return std::nullopt;
        return Integer_value{lhs.value >> rhs.value};
    }
    if (op == "<") return Integer_value{lhs.value < rhs.value};
    if (op == "<=") return Integer_value{lhs.value <= rhs.value};
    if (op == ">") return Integer_value{lhs.value > rhs.value};
    if (op == ">=") return Integer_value{lhs.value >= rhs.value};
    if (op == "==") return Integer_value{lhs.value == rhs.value};
    if (op == "!=") return Integer_value{lhs.value != rhs.value};
    if (op == "&") return Integer_value{lhs.value & rhs.value};
    if (op == "^") return Integer_value{lhs.value ^ rhs.value};
    if (op == "|") return Integer_value{lhs.value | rhs.value};
    if (op == "&&") return Integer_value{lhs.value && rhs.value};
    if (op == "||") return Integer_value{lhs.value || rhs.value};
    if (op == ",") return rhs;
    return std::nullopt;
}

}  // namespace c9ay::constant
