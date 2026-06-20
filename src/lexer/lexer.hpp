#pragma once

#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/trie_policy.hpp>
#include <meta>
#include <optional>
#include <string_view>

#include "base/error.hpp"
#include "base/util.hpp"
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
constexpr bool matches_keyword(std::string_view key, std::string_view enumerator_name) {
    constexpr std::string_view prefix = "K_";
    if (!enumerator_name.starts_with(prefix) ||
        key.size() != enumerator_name.size() - prefix.size()) {
        return false;
    }

    for (std::size_t i = 0; i < key.size(); ++i) {
        char reflected = enumerator_name[i + prefix.size()];
        if (reflected >= 'A' && reflected <= 'Z') {
            reflected += 'a' - 'A';
        }
        if (key[i] != reflected) {
            return false;
        }
    }
    return true;
}

}  // namespace reflect

token_type keyword_mapping(std::string_view key) {
    template for (constexpr auto enumerator :
                  std::define_static_array(
                      std::meta::enumerators_of(^^token_type))) {
        constexpr std::string_view name = std::meta::identifier_of(enumerator);
        if constexpr (name.starts_with("K_")) {
            if (reflect::matches_keyword(key, name)) {
                return [:enumerator:];
            }
        }
    }
    return token_type::IDENTIFIER;
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

bool is_operator(Reader &reader) {
    return is_one_of<"+-*/%<>">(ch);
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
    while (is_operator(reader.next_char())) {
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
