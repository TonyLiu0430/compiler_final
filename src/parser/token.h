#pragma once
#include <cctype>
#include <format>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/error.h"
#include "base/util.hpp"

namespace c9ay {
struct Token {
    std::string_view raw;
    Token(std::string_view _raw) : raw(_raw) {}
    virtual ~Token() {}
};

bool is_alphabet(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool is_ws(char ch) {
    return is_one_of<" \t">(ch);
}

bool is_endl(char ch) {
    return is_one_of<"\n">(ch);
}

bool is_digit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

struct Identifer : Token {};

struct Keyword : Token {};

struct String_literals : Token {
    std::string content;
    String_literals(std::string_view _raw, std::string _content) : Token(_raw), content(std::move(_content)) {}
    static std::unique_ptr<String_literals> match(std::string_view source) {
        if (source.size() == 0) throw internal_error("string literals 參數為空");
        auto it = source.begin();
        if (*it != '\"') throw internal_error("string literals 必須由雙引號(\")開始");
        it++;
        std::string content;
        bool escape = false;
        for (; it != source.end(); ++it) {
            char cur = *it;
            if (cur == '\n') {
                throw invalid_argument("string literals 不得換行");
            }
            if (escape) {
                if (cur == 'n') {
                    content.push_back('\n');
                }
                else if (cur == 't') {
                    content.push_back('\t');
                }
                else if (cur == '\\') {
                    content.push_back('\\');
                }
                else if (cur == '\"') {
                    content.push_back('\"');
                }
                else {
                    throw invalid_argument("");
                }
                continue;
            }
            if (cur == '\\') {
                escape = true;
                continue;
            }
            else if (cur == '\"') {
                std::string_view raw = std::string_view(source.begin(), it);
                std::unique_ptr<String_literals> res = std::make_unique<String_literals>(raw, content);
                return res;
            }
            else {
                content.push_back(cur);
            }
        }
        throw invalid_argument("string literals 未閉合");
    }
};

struct Char_constant : Token {
    std::string content;
    Char_constant(std::string_view _raw, std::string _content) : Token(_raw), content(_content) {}
    static std::unique_ptr<Char_constant> match(std::string_view source) {
        if (source.size() == 0) throw internal_error("char constant 參數為空");
        auto it = source.begin();
        if (*it != '\'') throw internal_error("char constant 必須由單引號(\')開始");
        it++;

        std::string content;
        bool escape = false;
        for (; it != source.end(); ++it) {
            char cur = *it;
            if (cur == '\n') {
                throw invalid_argument("char constant 不得換行");
            }
            if (escape) {
                if (cur == 'n') {
                    content.push_back('\n');
                }
                else if (cur == 't') {
                    content.push_back('\t');
                }
                else if (cur == '\\') {
                    content.push_back('\\');
                }
                else if (cur == '\'') {
                    content.push_back('\'');
                }
                else {
                    throw invalid_argument(std::format("\\{} 並非合法的跳脫字元", cur));
                }
                continue;
            }
            if (cur == '\\') {
                escape = true;
                continue;
            }
            else if (cur == '\'') {
                std::string_view raw = std::string_view(source.begin(), it);
                std::unique_ptr<Char_constant> res = std::make_unique<Char_constant>(raw, content);
                return res;
            }
            else {
                content.push_back(cur);
            }
        }
        throw invalid_argument("char constant 未閉合");
    }
};

struct Numeric_constant : Token {
    static std::unique_ptr<Numeric_constant> match(std::string_view source) {
        if (source.size() == 0) throw internal_error("numeric constant 參數為空");
        if (!is_digit(source[0])) throw internal_error("numeric constant 不由數字起始");
        for (auto x : source) {
            if (!is_digit(x) && !is_alphabet(x) && !is_one_of<".">(x)) {
                break;
            }
        }
        return nullptr;
    }
};

class Tokenlizer {
    int line = 1;

public:
    std::vector<std::unique_ptr<Token>> tokenize(std::string_view source) {
        std::vector<std::unique_ptr<Token>> tokens;
        for (auto it = source.begin(); it != source.end(); ++it) {
            if (is_ws(*it)) {
                continue;
            }
            if (is_endl(*it)) {
                line++;
                continue;
            }
            if (*it == '\"') {
                // string_literals
                tokens.push_back(String_literals::match(std::string_view(it, source.end())));
            }
            if (*it == '\'') {
                // char_literals
                tokens.push_back(Char_constant::match(std::string_view(it, source.end())));
            }
            if (is_digit(*it)) {
                // numeric constant
                tokens.push_back(Numeric_constant::match(std::string_view(it, source.end())));
            }
        }
        return tokens;
    }
};
}  // namespace c9ay