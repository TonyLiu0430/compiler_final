#pragma once

#include <array>
#include <cctype>
#include <string_view>
#include <vector>

namespace c9ay::scanner {

enum class token_type {
    END,
    WHITESPACE,
    NEWLINE,
    IDENTIFIER,
    NUMBER,
    STRING_CONSTANT,
    CHAR_CONSTANT,
    COMMENT,
    PUNCTUATOR,
    INVALID
};

struct Token {
    std::string_view raw;
    token_type type;
    int left;
    int right;
};

inline bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

inline bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

struct Punctuator_trie {
    struct Node {
        std::array<int, 128> next;
        bool terminal = false;

        Node() {
            next.fill(-1);
        }
    };

    std::vector<Node> nodes;

    Punctuator_trie() {
        nodes.emplace_back();

        constexpr std::string_view punctuators[] = {
            "[",   "]",   "(",   ")",   "{",   "}",   ".",   "->",
            "++",  "--",  "&",   "*",   "+",   "-",   "~",   "!",
            "/",   "%",   "<<",  ">>",  "<",   ">",   "<=",  ">=",
            "==",  "!=",  "^",   "|",   "&&",  "||",  "?",   ":",
            ";",   "...", "=",   "*=",  "/=",  "%=",  "+=",  "-=",
            "<<=", ">>=", "&=",  "^=",  "|=",  ",",   "#",   "##"
        };

        for (auto punctuator : punctuators) {
            int node = 0;
            for (char ch : punctuator) {
                int idx = static_cast<unsigned char>(ch);
                if (nodes[node].next[idx] == -1) {
                    nodes[node].next[idx] =
                        static_cast<int>(nodes.size());
                    nodes.emplace_back();
                }
                node = nodes[node].next[idx];
            }
            nodes[node].terminal = true;
        }
    }
};

inline const Punctuator_trie &get_punctuator_trie() {
    static const Punctuator_trie trie;
    return trie;
}

inline Token next_token(std::string_view source, int &cnt) {
    if (cnt >= static_cast<int>(source.size())) {
        return Token{"", token_type::END, cnt, cnt};
    }

    int begin = cnt;
    char ch = source[cnt];

    if (ch == '\n' || ch == '\r') {
        if (ch == '\r' &&
            cnt + 1 < static_cast<int>(source.size()) &&
            source[cnt + 1] == '\n') {
            cnt += 2;
        }
        else {
            cnt++;
        }
        return Token{
            source.substr(begin, cnt - begin),
            token_type::NEWLINE,
            begin,
            cnt
        };
    }

    if (std::isspace(static_cast<unsigned char>(ch))) {
        while (cnt < static_cast<int>(source.size()) &&
               source[cnt] != '\n' &&
               source[cnt] != '\r' &&
               std::isspace(static_cast<unsigned char>(source[cnt]))) {
            cnt++;
        }
        return Token{
            source.substr(begin, cnt - begin),
            token_type::WHITESPACE,
            begin,
            cnt
        };
    }

    if (is_identifier_start(ch)) {
        cnt++;
        while (cnt < static_cast<int>(source.size()) &&
               is_identifier_char(source[cnt])) {
            cnt++;
        }
        return Token{
            source.substr(begin, cnt - begin),
            token_type::IDENTIFIER,
            begin,
            cnt
        };
    }

    if (std::isdigit(static_cast<unsigned char>(ch))) {
        cnt++;
        while (cnt < static_cast<int>(source.size()) &&
               (std::isdigit(static_cast<unsigned char>(source[cnt])) ||
                source[cnt] == '.')) {
            cnt++;
        }
        return Token{
            source.substr(begin, cnt - begin),
            token_type::NUMBER,
            begin,
            cnt
        };
    }

    if (ch == '"' || ch == '\'') {
        char quote = source[cnt++];
        bool escape = false;
        while (cnt < static_cast<int>(source.size())) {
            char current = source[cnt++];
            if (escape) {
                escape = false;
            }
            else if (current == '\\') {
                escape = true;
            }
            else if (current == quote) {
                return Token{
                    source.substr(begin, cnt - begin),
                    quote == '"' ? token_type::STRING_CONSTANT
                                 : token_type::CHAR_CONSTANT,
                    begin,
                    cnt
                };
            }
        }
        return Token{
            source.substr(begin, cnt - begin),
            token_type::INVALID,
            begin,
            cnt
        };
    }

    if (ch == '/' &&
        cnt + 1 < static_cast<int>(source.size()) &&
        source[cnt + 1] == '/') {
        cnt += 2;
        while (cnt < static_cast<int>(source.size()) &&
               source[cnt] != '\n' &&
               source[cnt] != '\r') {
            cnt++;
        }
        return Token{
            source.substr(begin, cnt - begin),
            token_type::COMMENT,
            begin,
            cnt
        };
    }

    if (ch == '/' &&
        cnt + 1 < static_cast<int>(source.size()) &&
        source[cnt + 1] == '*') {
        cnt += 2;
        while (cnt < static_cast<int>(source.size())) {
            if (source[cnt] == '*' &&
                cnt + 1 < static_cast<int>(source.size()) &&
                source[cnt + 1] == '/') {
                cnt += 2;
                return Token{
                    source.substr(begin, cnt - begin),
                    token_type::COMMENT,
                    begin,
                    cnt
                };
            }
            cnt++;
        }
        return Token{
            source.substr(begin, cnt - begin),
            token_type::INVALID,
            begin,
            cnt
        };
    }

    const auto &trie = get_punctuator_trie();
    int node = 0;
    int probe = cnt;
    int matched = -1;
    while (probe < static_cast<int>(source.size())) {
        unsigned char current =
            static_cast<unsigned char>(source[probe]);
        if (current >= 128 || trie.nodes[node].next[current] == -1) {
            break;
        }
        node = trie.nodes[node].next[current];
        probe++;
        if (trie.nodes[node].terminal) {
            matched = probe;
        }
    }

    if (matched != -1) {
        cnt = matched;
        return Token{
            source.substr(begin, cnt - begin),
            token_type::PUNCTUATOR,
            begin,
            cnt
        };
    }

    cnt++;
    return Token{
        source.substr(begin, 1),
        token_type::INVALID,
        begin,
        cnt
    };
}

}  // namespace c9ay::scanner
