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
}  // namespace c9ay