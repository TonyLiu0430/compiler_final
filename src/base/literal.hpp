#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace c9ay::literal {

inline std::optional<char> decode_escape(char ch) {
    if (ch == 'n') return '\n';
    if (ch == 't') return '\t';
    if (ch == 'f') return '\f';
    if (ch == '\'') return '\'';
    if (ch == '"') return '"';
    if (ch == '\\') return '\\';
    return std::nullopt;
}

inline std::optional<std::string> decode_string(
    std::string_view raw) {
    if (raw.size() < 2 ||
        raw.front() != '"' ||
        raw.back() != '"') {
        return std::nullopt;
    }

    std::string result;
    for (int i = 1; i + 1 < static_cast<int>(raw.size()); i++) {
        if (raw[i] != '\\') {
            result.push_back(raw[i]);
            continue;
        }
        i++;
        if (i >= static_cast<int>(raw.size()) - 1) {
            return std::nullopt;
        }
        auto decoded = decode_escape(raw[i]);
        if (!decoded) return std::nullopt;
        result.push_back(*decoded);
    }
    return result;
}

inline std::optional<long long> decode_character(
    std::string_view raw) {
    if (raw.size() < 3 ||
        raw.front() != '\'' ||
        raw.back() != '\'') {
        return std::nullopt;
    }
    if (raw[1] != '\\') {
        if (raw.size() != 3) return std::nullopt;
        return static_cast<unsigned char>(raw[1]);
    }
    if (raw.size() != 4) return std::nullopt;

    auto decoded = decode_escape(raw[2]);
    if (!decoded) return std::nullopt;
    return static_cast<unsigned char>(*decoded);
}

}  // namespace c9ay::literal
