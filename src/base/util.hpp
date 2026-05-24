#pragma once

#include <utility>
namespace c9ay {
template <typename T, std::size_t N>
struct string_wrapper {
    constexpr string_wrapper(const T (&str)[N]) {
        std::copy(str, str + N, value);
    }
    T value[N]{};
};

template <string_wrapper chars>
constexpr bool is_one_of(char ch) {
    for (auto c : chars.value) {
        if (ch == c) return true;
    }
    return false;
}

bool is_alphabet(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

bool is_ws_endl(char ch) {
    return is_one_of<" \t\n\r">(ch);
}
}  // namespace c9ay