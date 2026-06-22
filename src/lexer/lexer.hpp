#pragma once

#include <meta>
#include <string>
#include <string_view>
#include <vector>

#include "base/error.hpp"
#include "base/util.hpp"
#include "lexer/scanner.hpp"
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
    K_FOR,
    K_SWITCH,
    K_CASE,
    K_DEFAULT,
    K_WHILE,
    K_DO,
    K_ELSE,
    K_RETURN,
    K_BREAK,
    K_CONTINUE,
    K_STRUCT,
    K_SIZEOF,
    K_ALIGNOF,
    K_TYPEDEF,
    K_CONST,
    K_STATIC
};

struct Token {
    std::string_view raw;
    token_type type;
    int left;
    int right;

    Token() = default;

    Token(
        std::string_view _raw,
        token_type _type,
        int _right)
        : raw(_raw),
          type(_type),
          left(_right - static_cast<int>(_raw.size())),
          right(_right) {}

    Token(
        std::string_view _raw,
        token_type _type,
        int _left,
        int _right)
        : raw(_raw),
          type(_type),
          left(_left),
          right(_right) {}
    template <token_type t>
    bool match(char c) const {
        if (type != t) return false;
        assert_c9ay(raw.size() == 1);
        return raw[0] == c;
    }
    template <token_type t>
    bool match(std::string_view s) const {
        if (type != t) return false;
        assert_c9ay(raw.size() != 1);
        return raw == s;
    }
    template <token_type t>
    bool match() const {
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

inline token_type keyword_mapping(std::string_view key) {
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

inline bool is_punctuarter(char ch) {
    return is_one_of<"{}#;">(ch);
}

inline void validate_escape_sequence(
    Reader &reader,
    scanner::Token token) {
    for (int i = 1; i + 1 < static_cast<int>(token.raw.size()); i++) {
        if (token.raw[i] != '\\') continue;
        i++;
        if (!is_one_of<"ntf\'\"\\">(token.raw[i])) {
            reader.report_error(
                "not illegal escape char",
                token.left + i);
        }
    }
}

inline Token next_token(Reader &reader) {
    while (1) {
        int cnt = reader.get_cnt();
        scanner::Token token =
            scanner::next_token(reader.get_raw(), cnt);
        reader.set_cnt(cnt);

        if (token.type == scanner::token_type::WHITESPACE ||
            token.type == scanner::token_type::NEWLINE ||
            token.type == scanner::token_type::COMMENT) {
            continue;
        }

        if (token.type == scanner::token_type::END) {
            return Token("", token_type::END, token.left, token.right);
        }
        if (token.type == scanner::token_type::IDENTIFIER) {
            return Token(
                token.raw,
                keyword_mapping(token.raw),
                token.left,
                token.right);
        }
        if (token.type == scanner::token_type::NUMBER) {
            return Token(
                token.raw,
                token_type::NUMBER,
                token.left,
                token.right);
        }
        if (token.type == scanner::token_type::STRING_CONSTANT) {
            validate_escape_sequence(reader, token);
            return Token(
                token.raw,
                token_type::STRING_CONSTANT,
                token.left,
                token.right);
        }
        if (token.type == scanner::token_type::CHAR_CONSTANT) {
            validate_escape_sequence(reader, token);
            return Token(
                token.raw,
                token_type::CHAR_CONSTANT,
                token.left,
                token.right);
        }
        if (token.type == scanner::token_type::PUNCTUATOR) {
            token_type type =
                token.raw.size() == 1 &&
                is_punctuarter(token.raw.front())
                    ? token_type::PUNCTUATOR
                    : token_type::OPERATOR;
            return Token(token.raw, type, token.left, token.right);
        }

        if (!token.raw.empty() && token.raw.front() == '\'') {
            reader.report_error(
                "unterminated character constant",
                token.left,
                token.right);
        }
        else if (!token.raw.empty() && token.raw.front() == '"') {
            reader.report_error(
                "unterminated string literal",
                token.left,
                token.right);
        }
        else if (token.raw.starts_with("/*")) {
            reader.report_error(
                "unterminated block comment",
                token.left,
                token.right);
        }
        else {
            reader.report_error(
                "stray character in program",
                token.left,
                token.right);
        }
        return Token(
            token.raw,
            token_type::ERROR,
            token.left,
            token.right);
    }
}
}  // namespace impl

class Lexer;

class LexerMgr {
    Reader reader;
    std::vector<Token> tokens;
    bool end = false;

    void fetch_token(int idx) {
        if (end) return;
        while (static_cast<int>(tokens.size()) <= idx) {
            tokens.push_back(lexer::impl::next_token(reader));
            if (tokens.back().type == token_type::END) {
                end = true;
                break;
            }
        }
    }

public:
    LexerMgr(Reader &_reader) : reader(_reader) {}

    bool has_token(int idx) {
        fetch_token(idx);
        return idx < static_cast<int>(tokens.size()) &&
               tokens[idx].type != token_type::END;
    }

    Token get_token(int idx) {
        fetch_token(idx);
        assert_c9ay(0 <= idx && idx < static_cast<int>(tokens.size()));
        return tokens[idx];
    }

    Lexer get_lexer();

    void report_error(const std::string &error_msg, int idx) {
        fetch_token(idx);
        if (idx < static_cast<int>(tokens.size())) {
            reader.report_error(
                error_msg,
                tokens[idx].left,
                tokens[idx].right);
        }
        else {
            reader.report_error(error_msg);
        }
    }

    Diagnostic &diagnostic() {
        return reader.diagnostic();
    }

    const Diagnostic &diagnostic() const {
        return reader.diagnostic();
    }
};

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
    bool has_next(int offset) {
        return mgr.has_token(token_cnt + offset);
    }
    template <class T>
    void panic_recovery(bool consume_sync_token = true) {
        while (has_next()) {
            Token tok = next_token();
            if (T::Panic_sync::is_sync_token(tok)) {
                if (!consume_sync_token) {
                    prev_token();
                }
                return;
            }
        }
    }
    Token next_token() {
        return mgr.get_token(token_cnt++);
    }
    Token peek_next() {
        return mgr.get_token(token_cnt);
    }
    Token peek_next(int offset) {
        return mgr.get_token(token_cnt + offset);
    }
    Token peek_prev() {
        return mgr.get_token(token_cnt - 1);
    }
    Token prev_token() {
        return mgr.get_token(--token_cnt);
    }
    void report_error(const std::string &s) {
        mgr.report_error(s, token_cnt);
    }

    Diagnostic &diagnostic() {
        return mgr.diagnostic();
    }
};

inline Lexer LexerMgr::get_lexer() {
    return Lexer(*this);
}

}  // namespace c9ay::lexer
