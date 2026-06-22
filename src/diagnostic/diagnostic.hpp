#pragma once

#include <algorithm>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "diagnostic/source_map.hpp"

namespace c9ay {

enum class Diagnostic_level {
    NOTE_LEVEL,
    WARNING_LEVEL,
    ERROR_LEVEL,
    FATAL_LEVEL
};

struct Source_range {
    int begin = 0;
    int end = 0;
};

struct Diagnostic_message {
    Diagnostic_level level;
    Source_range range;
    std::string message;
};

class Diagnostic {
    std::string file_name;
    std::string_view source;
    std::shared_ptr<Source_map> source_map;
    std::vector<int> line_starts{0};
    std::vector<Diagnostic_message> messages;
    std::unordered_set<std::string> keys;
    int error_count = 0;
    int max_errors = 20;
    bool limit_reported = false;

    static std::string_view level_name(Diagnostic_level level) {
        switch (level) {
            case Diagnostic_level::NOTE_LEVEL:
                return "note";
            case Diagnostic_level::WARNING_LEVEL:
                return "warning";
            case Diagnostic_level::ERROR_LEVEL:
                return "error";
            case Diagnostic_level::FATAL_LEVEL:
                return "fatal error";
        }
        return "error";
    }

    std::pair<int, int> locate(int position) const {
        position = std::clamp(
            position,
            0,
            static_cast<int>(source.size()));
        auto line = static_cast<int>(
            std::upper_bound(
                line_starts.begin(),
                line_starts.end(),
                position) -
            line_starts.begin()) - 1;
        return {
            line,
            position - line_starts[line]
        };
    }

    std::string_view source_line(int line) const {
        int begin = line_starts[line];
        int end = line + 1 < static_cast<int>(line_starts.size())
            ? line_starts[line + 1] - 1
            : static_cast<int>(source.size());
        if (end > begin && source[end - 1] == '\r') end--;
        return source.substr(begin, end - begin);
    }

public:
    struct Checkpoint {
        int message_count;
        int error_count;
        bool limit_reported;
    };

    Diagnostic(
        std::string_view _file_name,
        std::string_view _source,
        std::shared_ptr<Source_map> _source_map = nullptr)
        : file_name(_file_name),
          source(_source),
          source_map(std::move(_source_map)) {
        for (int i = 0; i < static_cast<int>(source.size()); i++) {
            if (source[i] == '\n') line_starts.push_back(i + 1);
        }
    }

    void report(
        Diagnostic_level level,
        std::string_view message,
        Source_range range) {
        range.begin = std::clamp(
            range.begin,
            0,
            static_cast<int>(source.size()));
        range.end = std::clamp(
            range.end,
            range.begin,
            static_cast<int>(source.size()));

        bool is_error =
            level == Diagnostic_level::ERROR_LEVEL ||
            level == Diagnostic_level::FATAL_LEVEL;
        if (is_error && error_count >= max_errors) {
            if (!limit_reported) {
                limit_reported = true;
                messages.push_back({
                    Diagnostic_level::FATAL_LEVEL,
                    range,
                    "too many errors emitted, stopping now"
                });
            }
            return;
        }

        auto key = std::format(
            "{}:{}:{}",
            range.begin,
            static_cast<int>(level),
            message);
        if (!keys.insert(key).second) return;

        messages.push_back({
            level,
            range,
            std::string(message)
        });
        if (is_error) error_count++;
    }

    void error(std::string_view message, Source_range range) {
        report(Diagnostic_level::ERROR_LEVEL, message, range);
    }

    void warning(std::string_view message, Source_range range) {
        report(Diagnostic_level::WARNING_LEVEL, message, range);
    }

    void note(std::string_view message, Source_range range) {
        report(Diagnostic_level::NOTE_LEVEL, message, range);
    }

    bool has_error() const {
        return error_count != 0;
    }

    int errors() const {
        return error_count;
    }

    const std::vector<Diagnostic_message> &all() const {
        return messages;
    }

    Checkpoint checkpoint() const {
        return {
            static_cast<int>(messages.size()),
            error_count,
            limit_reported
        };
    }

    void rollback(Checkpoint checkpoint) {
        messages.resize(checkpoint.message_count);
        error_count = checkpoint.error_count;
        limit_reported = checkpoint.limit_reported;
        keys.clear();
        for (auto &message : messages) {
            keys.insert(std::format(
                "{}:{}:{}",
                message.range.begin,
                static_cast<int>(message.level),
                message.message));
        }
    }

    std::string format(const Diagnostic_message &diagnostic) const {
        std::string mapped_file_name = file_name;
        std::string_view mapped_source = source;
        std::vector<int> mapped_line_starts = line_starts;
        int mapped_begin = diagnostic.range.begin;
        int mapped_end = diagnostic.range.end;
        std::vector<Include_site> include_stack;

        if (source_map) {
            auto begin = source_map->map(diagnostic.range.begin);
            auto end = source_map->map(
                std::max(
                    diagnostic.range.begin,
                    diagnostic.range.end - 1));
            if (begin.file) {
                mapped_file_name = begin.file->name;
                mapped_source = begin.file->source;
                mapped_line_starts = begin.file->line_starts;
                mapped_begin = begin.position;
                mapped_end =
                    end.file == begin.file
                        ? end.position + 1
                        : begin.position + 1;
                include_stack = std::move(begin.include_stack);
            }
        }

        auto locate_mapped = [&](int position) {
            position = std::clamp(
                position,
                0,
                static_cast<int>(mapped_source.size()));
            int line = static_cast<int>(
                std::upper_bound(
                    mapped_line_starts.begin(),
                    mapped_line_starts.end(),
                    position) -
                mapped_line_starts.begin()) - 1;
            return std::pair(
                line,
                position - mapped_line_starts[line]);
        };
        auto [line, column] = locate_mapped(mapped_begin);
        int line_begin = mapped_line_starts[line];
        int line_end =
            line + 1 < static_cast<int>(mapped_line_starts.size())
                ? mapped_line_starts[line + 1] - 1
                : static_cast<int>(mapped_source.size());
        if (line_end > line_begin &&
            mapped_source[line_end - 1] == '\r') {
            line_end--;
        }
        auto text = mapped_source.substr(
            line_begin,
            line_end - line_begin);
        int width = std::max(
            1,
            mapped_end - mapped_begin);
        width = std::min(
            width,
            std::max(1, static_cast<int>(text.size()) - column));

        std::string marker;
        for (int i = 0; i < column; i++) {
            marker.push_back(
                i < static_cast<int>(text.size()) &&
                text[i] == '\t'
                    ? '\t'
                    : ' ');
        }
        marker.push_back('^');
        marker.append(width - 1, '~');

        std::string result;
        if (!include_stack.empty()) {
            for (int i = 0;
                 i < static_cast<int>(include_stack.size());
                 i++) {
                auto &site = include_stack[i];
                auto &file = source_map->file(site.file);
                auto [include_line, include_column] =
                    file.locate(site.position);
                result += std::format(
                    "{}{}:{}:{}:\n",
                    i == 0
                        ? "In file included from "
                        : "                 from ",
                    file.name,
                    include_line + 1,
                    include_column + 1);
            }
        }
        result += std::format(
            "{}:{}:{}: {}: {}\n"
            " {:>{}} | {}\n"
            " {} | {}\n",
            mapped_file_name,
            line + 1,
            column + 1,
            level_name(diagnostic.level),
            diagnostic.message,
            line + 1,
            static_cast<int>(
                std::to_string(line + 1).size()),
            text,
            std::string(
                std::to_string(line + 1).size(),
                ' '),
            marker);
        return result;
    }

    std::string format_all() const {
        std::string result;
        for (auto &message : messages) {
            result += format(message);
        }
        return result;
    }

    void print(std::ostream &output) const {
        output << format_all();
    }
};

}  // namespace c9ay
