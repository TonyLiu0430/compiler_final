#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

#include "base/error.hpp"
#include "base/util.hpp"
#include "reader/reader.hpp"

#ifdef ERROR
#undef ERROR
#endif

namespace c9ay {
enum class token_type {
    ERROR,
    IDENTIFIER,
    STRING_CONSTANT,
    NUMBER,
    CHAR_CONSTANT,
    PUNCTUATOR,
    K_IF,
    K_WHILE,
    K_ELSE,
    K_RETURN
};

token_type keyword_mapping(std::string_view key) {
    static const std::unordered_map<std::string_view, token_type> table = {
        {"if", token_type::K_IF}, {"while", token_type::K_WHILE}, {"return", token_type::K_RETURN}};
    if (table.contains(key)) {
        return table.at(key);
    }
    else {
        return token_type::IDENTIFIER;
    }
}

struct Token {
    std::string_view raw;
    token_type type;
    int right;
};

Token match_char_str(Reader &reader) {
    Reader begin = reader;
    auto start = reader.next_char();
    token_type type;
    if (start == '\'') {
        type = token_type::CHAR_CONSTANT;
    }
    else if (start == '\"') {
        type = token_type::STRING_CONSTANT;
    }
    else {
        assert_c9ay(false);
    }

    bool escape = false;
    while (reader.has_next()) {
        auto c = reader.next_char();
        if (escape) {
            escape = false;
            if (!is_one_of<"ntf\'\"\\">(c)) {
                reader.report_error("not illegal escape char");
            }
        }
        else if (c == '\\') {
            escape = true;
        }
        else if (c == start) {
            return Token(reader.diff(begin), type, reader.get_cnt());
        }
    }

    if (type == token_type::CHAR_CONSTANT) {
        reader.report_error("未閉合 char constant");
    }
    else {
        reader.report_error("未閉合 string constant");
    }
    return Token(reader.diff(begin), token_type::ERROR, reader.get_cnt());
}

Token match_identifier_keyword(Reader &reader) {
    Reader begin = reader;
    while (reader.has_next()) {
        auto c = reader.peek_next();
        if (is_alphabet(c) || is_digit(c) || c == '_') {
            reader.next_char();
        }
        else {
            break;
        }
    }
    auto raw = reader.diff(begin);
    return Token(raw, keyword_mapping(raw), reader.get_cnt());
}

Token match_numbers(Reader &reader) {
    Reader begin = reader;
    while (reader.has_next()) {
        auto c = reader.peek_next();
        if (is_digit(c) || c == '.') {
            reader.next_char();
        }
        else {
            return Token(reader.diff(begin), token_type::NUMBER, reader.get_cnt());
        }
    }
    return Token(reader.diff(begin), token_type::NUMBER, reader.get_cnt());
}

Token match_punctuarter(Reader &reader) {
    Reader begin = reader;
    if (is_one_of<"{}#;()<>+-*">(reader.next_char())) {
        return Token(reader.diff(begin), token_type::PUNCTUATOR, reader.get_cnt());
    }
    return Token(reader.diff(begin), token_type::ERROR, reader.get_cnt());
}

Token next_token(Reader &reader) {
    while (reader.has_next()) {
        auto c = reader.peek_next();
        while (is_ws(c)) {
            if (!reader.has_next()) {
                return Token("", token_type::ERROR, 0);
            }
            reader.next_char();
            c = reader.peek_next();
        }
        if (c == '\'' || c == '\"') {
            // char constant or string constant
            return match_char_str(reader);
        }
        else if (is_alphabet(c)) {
            // identifier or keyword
            return match_identifier_keyword(reader);
        }
        else if (is_digit(c)) {
            // number
            return match_numbers(reader);
        }
        else if (is_one_of<"{}#;()<>+-*">(c)) {
            return match_punctuarter(reader);
        }
    }
    return Token("", token_type::ERROR, 0);
}
}  // namespace c9ay