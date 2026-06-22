#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lexer/scanner.hpp"
#include "preprocessor/constant_expression.hpp"
#include "reader/reader.hpp"

namespace c9ay::preprocessor {

struct Macro {
    bool function_like = false;
    bool variadic = false;
    std::vector<std::string> parameters;
    std::string replacement;
};

class Preprocessor {
    struct Conditional_state {
        bool parent_active;
        bool branch_taken;
        bool active;
    };

    std::unordered_map<std::string, Macro> macros;
    std::vector<std::filesystem::path> include_paths;
    Source_range current_range;

    static scanner::Token next_non_trivia(
        std::string_view text,
        int &cnt) {
        while (1) {
            auto token = scanner::next_token(text, cnt);
            if (token.type != scanner::token_type::WHITESPACE &&
                token.type != scanner::token_type::COMMENT) {
                return token;
            }
        }
    }

    static std::string_view trim_left(std::string_view text) {
        while (!text.empty() &&
               std::isspace(static_cast<unsigned char>(text.front()))) {
            text.remove_prefix(1);
        }
        return text;
    }

    static std::string_view trim(std::string_view text) {
        text = trim_left(text);
        while (!text.empty() &&
               std::isspace(static_cast<unsigned char>(text.back()))) {
            text.remove_suffix(1);
        }
        return text;
    }

    static std::string read_file(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error(
                "cannot open include file: " + path.string());
        }
        return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    }

    static std::string splice_lines(std::string_view source) {
        std::string result;
        for (int i = 0; i < static_cast<int>(source.size()); i++) {
            if (source[i] == '\\' &&
                i + 1 < static_cast<int>(source.size()) &&
                source[i + 1] == '\n') {
                i++;
                continue;
            }
            if (source[i] == '\\' &&
                i + 2 < static_cast<int>(source.size()) &&
                source[i + 1] == '\r' &&
                source[i + 2] == '\n') {
                i += 2;
                continue;
            }
            result.push_back(source[i]);
        }
        return result;
    }

    static std::pair<std::vector<std::string>, int> parse_arguments(
        std::string_view text,
        int open_parenthesis) {
        std::vector<std::string> arguments;
        int depth = 1;
        int begin = open_parenthesis + 1;
        int cnt = begin;

        while (1) {
            auto token = scanner::next_token(text, cnt);
            if (token.type == scanner::token_type::END ||
                token.type == scanner::token_type::INVALID) {
                return {{}, -1};
            }
            if (token.type != scanner::token_type::PUNCTUATOR) {
                continue;
            }

            if (token.raw == "(") {
                depth++;
            }
            else if (token.raw == ")") {
                depth--;
                if (depth == 0) {
                    auto argument =
                        trim(text.substr(begin, token.left - begin));
                    if (!argument.empty() || !arguments.empty()) {
                        arguments.emplace_back(argument);
                    }
                    return {std::move(arguments), token.right};
                }
            }
            else if (token.raw == "," && depth == 1) {
                arguments.emplace_back(
                    trim(text.substr(begin, token.left - begin)));
                begin = token.right;
            }
        }
    }

    static std::string substitute_parameters(
        const Macro &macro,
        const std::vector<std::string> &raw_arguments,
        const std::vector<std::string> &expanded_arguments) {
        std::unordered_map<std::string_view, std::string_view> values;
        std::unordered_map<std::string_view, std::string_view> raw_values;
        for (int i = 0; i < static_cast<int>(macro.parameters.size()); i++) {
            values[macro.parameters[i]] = expanded_arguments[i];
            raw_values[macro.parameters[i]] = raw_arguments[i];
        }

        auto stringify = [](std::string_view argument) {
            std::string value = "\"";
            bool pending_space = false;
            for (char ch : trim(argument)) {
                if (std::isspace(static_cast<unsigned char>(ch))) {
                    pending_space = true;
                    continue;
                }
                if (pending_space && value.size() != 1) {
                    value.push_back(' ');
                }
                pending_space = false;
                if (ch == '\\' || ch == '"') value.push_back('\\');
                value.push_back(ch);
            }
            value.push_back('"');
            return value;
        };

        auto trim_result_right = [](std::string &value) {
            while (!value.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(value.back()))) {
                value.pop_back();
            }
        };

        std::string result;
        std::string_view text = macro.replacement;
        int cnt = 0;
        bool paste = false;
        while (1) {
            auto token = scanner::next_token(text, cnt);
            if (token.type == scanner::token_type::END) {
                break;
            }

            if (token.type == scanner::token_type::PUNCTUATOR &&
                token.raw == "#") {
                int probe = cnt;
                auto parameter = next_non_trivia(text, probe);
                auto found = raw_values.find(parameter.raw);
                if (parameter.type == scanner::token_type::IDENTIFIER &&
                    found != raw_values.end()) {
                    result += stringify(found->second);
                    cnt = probe;
                    continue;
                }
            }

            if (token.type == scanner::token_type::PUNCTUATOR &&
                token.raw == "##") {
                trim_result_right(result);
                paste = true;
                continue;
            }

            if (paste &&
                (token.type == scanner::token_type::WHITESPACE ||
                 token.type == scanner::token_type::COMMENT)) {
                continue;
            }

            if (token.type != scanner::token_type::IDENTIFIER) {
                result.append(token.raw);
                paste = false;
                continue;
            }

            int probe = cnt;
            auto next = next_non_trivia(text, probe);
            bool before_paste =
                next.type == scanner::token_type::PUNCTUATOR &&
                next.raw == "##";
            std::string_view replacement;
            bool has_replacement = false;
            if (paste || before_paste) {
                auto found = raw_values.find(token.raw);
                if (found != raw_values.end()) {
                    replacement = found->second;
                    has_replacement = true;
                }
            }
            else {
                auto found = values.find(token.raw);
                if (found != values.end()) {
                    replacement = found->second;
                    has_replacement = true;
                }
            }
            if (!has_replacement) {
                result.append(token.raw);
            }
            else {
                result.append(replacement);
            }
            paste = false;
        }
        return result;
    }

    std::string expand_text(
        std::string_view text,
        std::unordered_set<std::string> disabled = {}) {
        std::string result;
        int cnt = 0;

        while (1) {
            auto token = scanner::next_token(text, cnt);
            if (token.type == scanner::token_type::END) {
                break;
            }
            if (token.type == scanner::token_type::COMMENT) {
                result.push_back(' ');
                continue;
            }
            if (token.type != scanner::token_type::IDENTIFIER) {
                result.append(token.raw);
                continue;
            }

            std::string name(token.raw);
            auto found = macros.find(name);
            if (found == macros.end() || disabled.contains(name)) {
                result.append(name);
                continue;
            }

            Macro &macro = found->second;
            auto next_disabled = disabled;
            next_disabled.insert(name);

            if (!macro.function_like) {
                result += expand_text(macro.replacement, std::move(next_disabled));
                continue;
            }

            int probe = cnt;
            auto open = next_non_trivia(text, probe);
            if (open.type != scanner::token_type::PUNCTUATOR ||
                open.raw != "(") {
                result.append(name);
                continue;
            }

            auto [arguments, end] =
                parse_arguments(text, open.left);
            if (arguments.empty() &&
                !macro.parameters.empty()) {
                arguments.emplace_back();
            }
            int fixed_parameters =
                static_cast<int>(macro.parameters.size()) -
                (macro.variadic ? 1 : 0);
            if (end == -1 ||
                (!macro.variadic &&
                 arguments.size() != macro.parameters.size()) ||
                (macro.variadic &&
                 static_cast<int>(arguments.size()) < fixed_parameters)) {
                result.append(name);
                continue;
            }

            if (macro.variadic) {
                std::string variadic;
                for (int i = fixed_parameters;
                     i < static_cast<int>(arguments.size());
                     i++) {
                    if (!variadic.empty()) variadic += ", ";
                    variadic += arguments[i];
                }
                arguments.resize(fixed_parameters);
                arguments.push_back(std::move(variadic));
            }

            auto raw_arguments = arguments;
            for (auto &argument : arguments) {
                argument = expand_text(argument);
            }
            std::string replacement =
                substitute_parameters(
                    macro,
                    raw_arguments,
                    arguments);
            result += expand_text(replacement, std::move(next_disabled));
            cnt = end;
        }

        return result;
    }

    std::string replace_defined(std::string_view expression) {
        std::string result;
        int cnt = 0;

        while (1) {
            auto token = scanner::next_token(expression, cnt);
            if (token.type == scanner::token_type::END) {
                break;
            }
            if (token.type != scanner::token_type::IDENTIFIER ||
                token.raw != "defined") {
                result.append(token.raw);
                continue;
            }

            int probe = cnt;
            auto next = next_non_trivia(expression, probe);
            bool parenthesized =
                next.type == scanner::token_type::PUNCTUATOR &&
                next.raw == "(";
            if (parenthesized) {
                next = next_non_trivia(expression, probe);
            }

            if (next.type != scanner::token_type::IDENTIFIER) {
                result.append(token.raw);
                continue;
            }

            std::string identifier(next.raw);
            if (parenthesized) {
                auto close = next_non_trivia(expression, probe);
                if (close.type != scanner::token_type::PUNCTUATOR ||
                    close.raw != ")") {
                    result.append(token.raw);
                    continue;
                }
            }

            result += macros.contains(identifier) ? "1" : "0";
            cnt = probe;
        }
        return result;
    }

    static std::string replace_condition_identifiers(
        std::string_view expression) {
        std::string result;
        int cnt = 0;
        while (1) {
            auto token = scanner::next_token(expression, cnt);
            if (token.type == scanner::token_type::END) break;
            if (token.type == scanner::token_type::IDENTIFIER) {
                result.push_back('0');
            }
            else {
                result.append(token.raw);
            }
        }
        return result;
    }

    bool evaluate_condition(std::string_view expression) {
        std::string replaced = replace_defined(expression);
        std::string expanded = expand_text(replaced);
        std::string normalized =
            replace_condition_identifiers(expanded);
        auto value = Constant_expression(normalized).evaluate();
        return value && value->value != 0;
    }

    void define_macro(std::string_view body) {
        body = trim_left(body);
        int cnt = 0;
        auto name_token = scanner::next_token(body, cnt);
        if (name_token.type != scanner::token_type::IDENTIFIER) return;

        std::string name(name_token.raw);
        Macro macro;

        int after_name = cnt;
        auto open = scanner::next_token(body, cnt);
        if (open.type == scanner::token_type::PUNCTUATOR &&
            open.raw == "(") {
            macro.function_like = true;

            auto parameter = next_non_trivia(body, cnt);
            if (parameter.type == scanner::token_type::PUNCTUATOR &&
                parameter.raw == ")") {
                macro.replacement =
                    std::string(trim_left(body.substr(parameter.right)));
                macros[name] = std::move(macro);
                return;
            }

            while (1) {
                if (parameter.type == scanner::token_type::PUNCTUATOR &&
                    parameter.raw == "...") {
                    macro.variadic = true;
                    macro.parameters.emplace_back("__VA_ARGS__");
                    auto close = next_non_trivia(body, cnt);
                    if (close.type != scanner::token_type::PUNCTUATOR ||
                        close.raw != ")") {
                        return;
                    }
                    macro.replacement =
                        std::string(trim_left(body.substr(close.right)));
                    break;
                }
                if (parameter.type != scanner::token_type::IDENTIFIER) {
                    return;
                }
                macro.parameters.emplace_back(parameter.raw);

                auto separator = next_non_trivia(body, cnt);
                if (separator.type != scanner::token_type::PUNCTUATOR) {
                    return;
                }
                if (separator.raw == ")") {
                    macro.replacement =
                        std::string(trim_left(body.substr(separator.right)));
                    break;
                }
                if (separator.raw != ",") return;
                parameter = next_non_trivia(body, cnt);
            }
        }
        else {
            macro.replacement =
                std::string(trim_left(body.substr(after_name)));
        }

        macros[name] = std::move(macro);
    }

    std::filesystem::path find_include(
        std::string_view name,
        bool local,
        const std::filesystem::path &current_file) {
        if (local && !current_file.empty()) {
            auto path = current_file.parent_path() / name;
            if (std::filesystem::exists(path)) return path;
        }
        for (auto &directory : include_paths) {
            auto path = directory / name;
            if (std::filesystem::exists(path)) return path;
        }
        throw std::runtime_error(
            "include file not found: " + std::string(name));
    }

    std::string process_source(
        std::string_view source,
        const std::filesystem::path &current_file) {
        std::string spliced = splice_lines(source);
        std::string result;
        std::vector<Conditional_state> conditions;

        int begin = 0;
        while (begin <= static_cast<int>(spliced.size())) {
            int end = static_cast<int>(spliced.find('\n', begin));
            bool has_newline = end != -1;
            if (!has_newline) end = static_cast<int>(spliced.size());

            std::string_view line(spliced.data() + begin, end - begin);
            current_range = {begin, end};
            std::string_view content = trim_left(line);
            bool active = conditions.empty() || conditions.back().active;

            int directive_cnt = 0;
            auto hash = scanner::next_token(content, directive_cnt);
            if (hash.type == scanner::token_type::PUNCTUATOR &&
                hash.raw == "#") {
                auto directive_token =
                    next_non_trivia(content, directive_cnt);
                std::string_view directive =
                    directive_token.type == scanner::token_type::IDENTIFIER
                        ? directive_token.raw
                        : std::string_view();
                std::string_view body =
                    trim_left(content.substr(directive_token.right));

                if (directive == "if" ||
                    directive == "ifdef" ||
                    directive == "ifndef") {
                    bool parent_active = active;
                    bool condition = false;
                    if (directive == "if") {
                        condition = evaluate_condition(body);
                    }
                    else {
                        std::string name(trim(body));
                        condition = macros.contains(name);
                        if (directive == "ifndef") condition = !condition;
                    }
                    condition &= parent_active;
                    conditions.push_back({
                        parent_active,
                        condition,
                        condition
                    });
                }
                else if (directive == "elif") {
                    if (conditions.empty()) {
                        throw std::runtime_error("#elif without #if");
                    }
                    auto &state = conditions.back();
                    state.active =
                        state.parent_active &&
                        !state.branch_taken &&
                        evaluate_condition(body);
                    state.branch_taken |= state.active;
                }
                else if (directive == "else") {
                    if (conditions.empty()) {
                        throw std::runtime_error("#else without #if");
                    }
                    auto &state = conditions.back();
                    state.active =
                        state.parent_active && !state.branch_taken;
                    state.branch_taken = true;
                }
                else if (directive == "endif") {
                    if (conditions.empty()) {
                        throw std::runtime_error("#endif without #if");
                    }
                    conditions.pop_back();
                }
                else if (active && directive == "define") {
                    define_macro(body);
                }
                else if (active && directive == "undef") {
                    macros.erase(std::string(trim(body)));
                }
                else if (active && directive == "include") {
                    std::string expanded = expand_text(body);
                    std::string_view include = trim(expanded);
                    bool local =
                        include.size() >= 2 && include.front() == '"';
                    bool system =
                        include.size() >= 2 && include.front() == '<';
                    if (!local && !system) {
                        throw std::runtime_error("invalid #include");
                    }
                    char close = local ? '"' : '>';
                    int close_at =
                        static_cast<int>(include.find(close, 1));
                    if (close_at == -1) {
                        throw std::runtime_error("invalid #include");
                    }
                    auto path = find_include(
                        include.substr(1, close_at - 1),
                        local,
                        current_file);
                    auto include_range = current_range;
                    try {
                        result += process_source(read_file(path), path);
                    }
                    catch (...) {
                        current_range = include_range;
                        throw;
                    }
                }
                else if (active && directive == "error") {
                    throw std::runtime_error(std::string(trim(body)));
                }
            }
            else if (active) {
                result += expand_text(line);
                if (has_newline) result.push_back('\n');
            }
            else if (has_newline) {
                result.push_back('\n');
            }

            if (!has_newline) break;
            begin = end + 1;
        }

        if (!conditions.empty()) {
            throw std::runtime_error("unterminated conditional directive");
        }
        return result;
    }

public:
    void add_include_path(const std::filesystem::path &path) {
        include_paths.push_back(path);
    }

    std::string process(
        std::string_view source,
        const std::filesystem::path &file_name = {}) {
        return process_source(source, file_name);
    }

    std::string process(
        Reader &reader,
        const std::filesystem::path &file_name = {}) {
        try {
            return process_source(reader.get_raw(), file_name);
        }
        catch (const std::exception &error) {
            reader.diagnostic().error(
                error.what(),
                current_range);
            throw;
        }
    }

    std::string process_file(const std::filesystem::path &path) {
        return process_source(read_file(path), path);
    }
};

}  // namespace c9ay::preprocessor
