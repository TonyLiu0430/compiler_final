#pragma once

#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/trie_policy.hpp>
#include <array>
#include <meta>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    K_FOR,
    K_WHILE,
    K_DO,
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
        if (type != t) return false;
        assert_c9ay(raw.size() == 1);
        return raw[0] == c;
    }
    template <token_type t>
    bool match(std::string_view s) {
        if (type != t) return false;
        assert_c9ay(raw.size() != 1);
        return raw == s;
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
    return is_one_of<"{}#;">(ch);
}

bool is_operator(char ch) {
    return is_one_of<"+-*/%<>=!&|^~.[]()?:,">(ch);
}

struct Operator_trie {
    struct Node {
        std::array<int, 128> next;
        bool terminal = false;

        Node() {
            next.fill(-1);
        }
    };

    std::vector<Node> nodes;

    Operator_trie() {
        nodes.emplace_back();

        constexpr std::string_view operators[] = {
            // postfix / member
            "[",   "]",   "(",   ")",   "++",  "--",  ".",   "->",

            // unary / arithmetic
            "+",   "-",   "*",   "/",   "%",   "!",   "~",

            // shift / comparison
            "<<",  ">>",  "<",   ">",   "<=",  ">=",  "==",  "!=",

            // bitwise / logical
            "&",   "^",   "|",   "&&",  "||",

            // assignment
            "=",   "*=",  "/=",  "%=",  "+=",  "-=",
            "<<=", ">>=", "&=",  "^=",  "|=",

            // conditional / comma
            "?",   ":",   ","
        };

        for (auto op : operators) {
            int node = 0;
            for (char ch : op) {
                int idx = static_cast<unsigned char>(ch);
                if (nodes[node].next[idx] == -1) {
                    nodes[node].next[idx] = static_cast<int>(nodes.size());
                    nodes.emplace_back();
                }
                node = nodes[node].next[idx];
            }
            nodes[node].terminal = true;
        }
    }
};

const Operator_trie &get_operator_trie() {
    static const Operator_trie trie;
    return trie;
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
    const auto &trie = get_operator_trie();
    int node = 0;
    int consumed = 0;
    int matched = -1;

    while (reader.has_next()) {
        auto ch = reader.peek_next();
        if (ch < 0 || ch >= 128) break;

        int next = trie.nodes[node].next[static_cast<int>(ch)];
        if (next == -1) break;

        reader.next_char();
        consumed++;
        node = next;
        if (trie.nodes[node].terminal) {
            matched = consumed;
        }
    }

    if (matched == -1) {
        while (consumed-- > 0) reader.prev_char();
        return std::nullopt;
    }
    while (consumed > matched) {
        reader.prev_char();
        consumed--;
    }

    return Token(reader.diff(begin), token_type::OPERATOR, reader.get_cnt());
}

bool match_comment(Reader &reader) {
    Reader begin = reader;
    if (reader.next_char() != '/') {
        reader.prev_char();
        return false;
    }
    if (!reader.has_next()) {
        reader.prev_char();
        return false;
    }

    auto second = reader.next_char();
    if (second == '/') {
        while (reader.has_next() && reader.peek_next() != '\n') {
            reader.next_char();
        }
        return true;
    }
    if (second == '*') {
        while (reader.has_next()) {
            auto ch = reader.next_char();
            if (ch == '*' && reader.has_next() && reader.peek_next() == '/') {
                reader.next_char();
                return true;
            }
        }
        reader.report_error("unterminated block comment", begin.get_cnt());
        return true;
    }

    reader.prev_char();
    reader.prev_char();
    return false;
}

Token next_token(Reader &reader) {
    while (reader.has_next()) {
        auto c = reader.peek_next();
        while (reader.has_next() && is_ws_endl(c)) {
            reader.next_char();
            if (!reader.has_next()) {
                return Token("", token_type::END, reader.get_cnt());
            }
            c = reader.peek_next();
        }

        if (c == '/' && match_comment(reader)) {
            continue;
        }

        std::optional<Token> tok;
        if (c == '\'' || c == '\"') {
            // char constant or string constant
            tok = match_char_str(reader);
        }
        else if (is_alphabet(c) || c == '_') {
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
        else if (is_operator(c)) {
            tok = match_operator(reader);
        }

        if (tok.has_value()) {
            return tok.value();
        }

        Reader begin = reader;
        reader.report_error("unknown character");
        reader.next_char();
        return Token(reader.diff(begin), token_type::ERROR, reader.get_cnt());
    }
    return Token("", token_type::END, reader.get_cnt());
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
            reader.report_error(error_msg, tokens[idx].right);
        }
        else {
            reader.report_error(error_msg);
        }
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
};

inline Lexer LexerMgr::get_lexer() {
    return Lexer(*this);
}

}  // namespace c9ay::lexer
