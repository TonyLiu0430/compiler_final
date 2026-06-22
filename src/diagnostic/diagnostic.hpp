#pragma once

#include <algorithm>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

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
        std::string_view _source)
        : file_name(_file_name),
          source(_source) {
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
        auto [line, column] = locate(diagnostic.range.begin);
        auto text = source_line(line);
        int width = std::max(
            1,
            diagnostic.range.end - diagnostic.range.begin);
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

        return std::format(
            "{}:{}:{}: {}: {}\n"
            " {:>{}} | {}\n"
            " {} | {}\n",
            file_name,
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
