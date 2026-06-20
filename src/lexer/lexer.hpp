#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "base/error.hpp"
#include "base/util.hpp"
#include "magic_enum.hpp"
#include "reader/reader.hpp"
#include "utility"

#ifdef ERROR
#undef ERROR
#endif

namespace c9ay::lexer {
enum class token_type {
    ERROR,
    END,
    IDENTIFIER,
    STRING_CONSTANT,
    NUMBER,
    CHAR_CONSTANT,
    PUNCTUATOR,
    OPERATOR,
    K_IF,
    K_WHILE,
    K_ELSE,
    K_RETURN,
    K_BREAK,
    K_CONTINUE,
    K_CONST,
    K_STATIC
};

struct Token {
    std::string_view raw;
    token_type type;
    int right;
    template <token_type t>
    bool match(char c) {
        assert_c9ay(raw.size() == 1);
        return type == t && raw[0] == c;
    }
    template <token_type t>
    bool match(std::string_view s) {
        assert_c9ay(raw.size() != 1);
        return type == t && raw == s;
    }
    template <token_type t>
    bool match() {
        return type == t;
    }
};

namespace impl {

namespace reflect {
// TODO

std::unordered_map<std::string_view, token_type> reflect_keyword() {
    std::unordered_map<std::string_view, token_type> res;
    for (int i = 0; i < magic_enum::enum_count<token_type>(); i++) {
        token_type type = magic_enum::enum_value<token_type>(i);
        std::string_view name = magic_enum::enum_name<token_type>(type);

        if (name.starts_with("K_")) {
            std::string *lower = new string(name.substr(2));
            for (auto &c : *lower) {
                if (c >= 'A' && c <= 'Z') {
                    c = (c - 'A') + 'a';
                }
            }
            std::string_view sv = *lower;
            res[sv] = type;
        }
    }
    return res;
}

}  // namespace reflect

token_type keyword_mapping(std::string_view key) {
    static const std::unordered_map<std::string_view, token_type> table = reflect::reflect_keyword();
    if (table.contains(key)) {
        return table.at(key);
    }
    else {
        return token_type::IDENTIFIER;
    }
}

std::optional<Token> match_char_str(Reader &reader) {
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
    return std::nullopt;
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

bool is_punctuarter(char ch) {
    return is_one_of<"{}#;()">(ch);
}

bool is_operator(char ch) {
    return is_one_of<"+-*/%">(ch);
}

std::optional<Token> match_punctuarter(Reader &reader) {
    Reader begin = reader;
    if (is_punctuarter(reader.next_char())) {
        return Token(reader.diff(begin), token_type::PUNCTUATOR, reader.get_cnt());
    }
    return std::nullopt;
}

std::optional<Token> match_operator(Reader &reader) {
    Reader begin = reader;
    if (is_punctuarter(reader.next_char())) {
        if (reader.peek_next() == '/') {
            reader.next_char();
            while (reader.peek_next() != '\n') {
                reader.next_char();
            }
        }
        return Token(reader.diff(begin), token_type::OPERATOR, reader.get_cnt());
    }
    return std::nullopt;
}

Token next_token(Reader &reader) {
    while (reader.has_next()) {
        auto c = reader.peek_next();
        while (is_ws_endl(c)) {
            if (!reader.has_next()) {
                return Token("", token_type::ERROR, 0);
            }
            reader.next_char();
            c = reader.peek_next();
        }
        std::optional<Token> tok;
        if (c == '\'' || c == '\"') {
            // char constant or string constant
            tok = match_char_str(reader);
        }
        else if (is_alphabet(c)) {
            // identifier or keyword
            tok = match_identifier_keyword(reader);
        }
        else if (is_digit(c)) {
            // number
            tok = match_numbers(reader);
        }
        else if (is_punctuarter(c)) {
            tok = match_punctuarter(reader);
        }

        if (tok.has_value()) {
            return tok.value();
        }
    }
    return Token("", token_type::END, 0);
}
}  // namespace impl

class Lexer {
    LexerMgr &mgr;
    int token_cnt = 0;

public:
    Lexer(LexerMgr &_mgr) : mgr(_mgr) {}
    Lexer(Lexer &other) : mgr(other.mgr), token_cnt(other.token_cnt) {}
    void sync(Lexer &other) {
        assert_c9ay(&mgr == &other.mgr);
        token_cnt = other.token_cnt;
    }
    bool has_next() {
        return mgr.has_token(token_cnt);
    }
    template <class T>
    void panic_recovery() {
        while (1) {
            Token tok = next_token();
            if (T::Panic_sync::is_sync_token(tok)) {
                next_token();
                return;
            }
        }
    }
    Token next_token() {
        mgr.get_token(token_cnt++);
    }
    Token peek_next() {
        mgr.get_token(token_cnt);
    }
    Token peek_prev() {
        mgr.get_token(token_cnt - 1);
    }
    Token prev_token() {
        mgr.get_token(--token_cnt);
    }
    void report_error(const std::string &s) {
        mgr.report_error(s, token_cnt);
    }
};

class LexerMgr {
    Reader reader;
    std::vector<Token> tokens;
    bool end = false;

    void fetch_token(int idx) {
        if (end) return;
        while (tokens.size() <= idx) {
            tokens.push_back(lexer::impl::next_token(reader));
            if (tokens.back().type == token_type::END) {
                end = true;
                break;
            }
        }
    }

public:
    LexerMgr(Reader &_reader) : reader(_reader) {}
    Token next_token() {
        Token token = lexer::impl::next_token(reader);
        tokens.push_back(token);
        return token;
    }

    bool has_token(int idx) {
        fetch_token(idx);
        return idx < tokens.size();
    }
    Token get_token(int idx) {
        fetch_token(idx);
        assert_c9ay(0 <= idx && idx < tokens.size());
        return tokens[idx];
    }
    Lexer get_lexer() {
        return Lexer(*this);
    }
    void report_error(const std::string &error_msg, int idx) {
        reader.report_error(error_msg, tokens[idx].right);
    }
};

}  // namespace c9ay::lexer