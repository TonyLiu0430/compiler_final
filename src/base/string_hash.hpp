#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace c9ay {

struct Transparent_string_hash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }

    std::size_t operator()(const std::string &value) const noexcept {
        return (*this)(std::string_view(value));
    }

    std::size_t operator()(const char *value) const noexcept {
        return (*this)(std::string_view(value));
    }
};

struct Transparent_string_equal {
    using is_transparent = void;

    bool operator()(
        std::string_view lhs,
        std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

}  // namespace c9ay
