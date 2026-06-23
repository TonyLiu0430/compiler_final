#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace c9ay::builtin {

enum class Printf_part_kind {
    TEXT,
    SIGNED_INTEGER,
    UNSIGNED_INTEGER,
    STRING,
    CHARACTER
};

struct Printf_part {
    Printf_part_kind kind;
    std::string text;
};

struct Printf_format {
    std::vector<Printf_part> parts;
    int arguments = 0;
};

inline std::optional<Printf_format> parse_printf_format(
    std::string_view format,
    std::string *error = nullptr) {
    Printf_format result;
    std::string text;

    auto fail = [&](std::string message)
        -> std::optional<Printf_format> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    auto flush_text = [&] {
        if (text.empty()) return;
        result.parts.push_back({
            Printf_part_kind::TEXT,
            std::move(text)
        });
        text.clear();
    };

    for (int i = 0; i < static_cast<int>(format.size()); i++) {
        if (format[i] != '%') {
            text.push_back(format[i]);
            continue;
        }
        if (i + 1 >= static_cast<int>(format.size())) {
            return fail("incomplete printf format specifier");
        }
        if (format[i + 1] == '%') {
            text.push_back('%');
            i++;
            continue;
        }

        flush_text();
        i++;
        int length = 0;
        if (format[i] == 'l') {
            length++;
            i++;
            if (i < static_cast<int>(format.size()) &&
                format[i] == 'l') {
                length++;
                i++;
            }
            if (i >= static_cast<int>(format.size())) {
                return fail("incomplete printf format specifier");
            }
        }

        Printf_part_kind kind;
        if (format[i] == 'd' || format[i] == 'i') {
            kind = Printf_part_kind::SIGNED_INTEGER;
        }
        else if (format[i] == 'u') {
            kind = Printf_part_kind::UNSIGNED_INTEGER;
        }
        else if (format[i] == 's') {
            if (length != 0) {
                return fail("unsupported printf string length modifier");
            }
            kind = Printf_part_kind::STRING;
        }
        else if (format[i] == 'c') {
            if (length != 0) {
                return fail("unsupported printf character length modifier");
            }
            kind = Printf_part_kind::CHARACTER;
        }
        else {
            return fail(
                "unsupported printf format specifier '%" +
                std::string(1, format[i]) + "'");
        }
        result.parts.push_back({kind, {}});
        result.arguments++;
    }

    flush_text();
    return result;
}

}  // namespace c9ay::builtin
