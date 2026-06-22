#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "base/error.hpp"
#include "diagnostic/diagnostic.hpp"

namespace c9ay {

class Reader {
    std::string file_name;
    std::string_view raw;
    std::shared_ptr<Diagnostic> diagnostics;
    int cnt = 0;

public:
    Reader(
        std::string_view _file,
        std::string_view _raw,
        std::shared_ptr<Source_map> source_map = nullptr)
        : file_name(_file),
          raw(_raw),
          diagnostics(std::make_shared<Diagnostic>(
              _file,
              _raw,
              std::move(source_map))) {}

    Reader(const Reader &other) = default;

    Reader clone() const {
        return Reader(*this);
    }

    void report_error(
        std::string_view message,
        int begin,
        int end) {
        diagnostics->error(message, {begin, end});
    }

    void report_error(
        std::string_view message,
        int position) {
        report_error(message, position, position + 1);
    }

    void report_error(std::string_view message) {
        report_error(message, cnt, cnt + 1);
    }

    Diagnostic &diagnostic() {
        return *diagnostics;
    }

    const Diagnostic &diagnostic() const {
        return *diagnostics;
    }

    char32_t next_char() {
        assert_c9ay(cnt < static_cast<int>(raw.size()));
        return raw[cnt++];
    }

    char32_t prev_char() {
        return raw[--cnt];
    }

    bool has_next() const {
        return cnt < static_cast<int>(raw.size());
    }

    char32_t peek_prev() const {
        return raw[cnt - 1];
    }

    char32_t peek_next() const {
        return raw[cnt];
    }

    std::string_view diff(const Reader &begin) const {
        return raw.substr(begin.cnt, cnt - begin.cnt);
    }

    int get_cnt() const {
        return cnt;
    }

    std::string_view get_raw() const {
        return raw;
    }

    std::string_view get_file_name() const {
        return file_name;
    }

    void set_cnt(int _cnt) {
        assert_c9ay(
            0 <= _cnt &&
            _cnt <= static_cast<int>(raw.size()));
        cnt = _cnt;
    }
};

}  // namespace c9ay
