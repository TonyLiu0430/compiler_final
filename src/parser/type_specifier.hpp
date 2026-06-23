#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "lexer/lexer.hpp"
#include "parser/type_names.hpp"

namespace c9ay::parser {

struct Parsed_type_specifier {
    lexer::Token first;
    std::string name;
    bool known;
};

inline bool is_builtin_type_word(std::string_view word) {
    return word == "void" ||
           word == "_Bool" ||
           word == "char" ||
           word == "short" ||
           word == "int" ||
           word == "long" ||
           word == "signed" ||
           word == "unsigned" ||
           word == "float" ||
           word == "double";
}

inline std::optional<Parsed_type_specifier> match_type_specifier(
    lexer::Lexer &lexer,
    bool allow_unknown = false) {
    if (!lexer.has_next() ||
        !lexer.peek_next().match<lexer::token_type::IDENTIFIER>()) {
        return std::nullopt;
    }

    auto first = lexer.peek_next();
    if (!is_builtin_type_word(first.raw)) {
        if (!allow_unknown &&
            !type_names::contains(first.raw)) {
            return std::nullopt;
        }
        lexer.next_token();
        return Parsed_type_specifier{
            first,
            std::string(first.raw),
            type_names::contains(first.raw)
        };
    }

    int signed_count = 0;
    int unsigned_count = 0;
    int short_count = 0;
    int long_count = 0;
    int int_count = 0;
    int char_count = 0;
    int float_count = 0;
    int double_count = 0;
    int void_count = 0;
    int bool_count = 0;

    while (lexer.has_next() &&
           lexer.peek_next().match<lexer::token_type::IDENTIFIER>() &&
           is_builtin_type_word(lexer.peek_next().raw)) {
        auto word = lexer.next_token().raw;
        signed_count += word == "signed";
        unsigned_count += word == "unsigned";
        short_count += word == "short";
        long_count += word == "long";
        int_count += word == "int";
        char_count += word == "char";
        float_count += word == "float";
        double_count += word == "double";
        void_count += word == "void";
        bool_count += word == "_Bool";
    }

    bool invalid =
        signed_count > 1 ||
        unsigned_count > 1 ||
        signed_count + unsigned_count > 1 ||
        short_count > 1 ||
        long_count > 2 ||
        int_count > 1 ||
        char_count > 1 ||
        float_count > 1 ||
        double_count > 1 ||
        void_count > 1 ||
        bool_count > 1;

    int special =
        char_count + float_count + double_count +
        void_count + bool_count;
    invalid |= special > 1;

    std::string name;
    if (!invalid && void_count) {
        invalid |= signed_count || unsigned_count ||
                   short_count || long_count || int_count;
        name = "void";
    }
    else if (!invalid && bool_count) {
        invalid |= signed_count || unsigned_count ||
                   short_count || long_count || int_count;
        name = "_Bool";
    }
    else if (!invalid && float_count) {
        invalid |= signed_count || unsigned_count ||
                   short_count || long_count || int_count;
        name = "float";
    }
    else if (!invalid && double_count) {
        invalid |= signed_count || unsigned_count ||
                   short_count || int_count || long_count > 1;
        name = long_count ? "long double" : "double";
    }
    else if (!invalid && char_count) {
        invalid |= short_count || long_count || int_count;
        name = unsigned_count
            ? "unsigned char"
            : signed_count
                ? "signed char"
                : "char";
    }
    else if (!invalid) {
        invalid |= short_count && long_count;
        if (short_count) {
            name = unsigned_count
                ? "unsigned short"
                : "short";
        }
        else if (long_count == 1) {
            name = unsigned_count
                ? "unsigned long"
                : "long";
        }
        else if (long_count == 2) {
            name = unsigned_count
                ? "unsigned long long"
                : "long long";
        }
        else {
            name = unsigned_count
                ? "unsigned int"
                : "int";
        }
    }

    if (invalid) {
        lexer.report_error("invalid type specifier combination");
        return Parsed_type_specifier{
            first,
            "<invalid-type>",
            true
        };
    }
    return Parsed_type_specifier{
        first,
        std::move(name),
        true
    };
}

}  // namespace c9ay::parser
