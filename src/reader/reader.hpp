#pragma once
#include <format>
#include <print>
#include <string>
#include <string_view>

namespace c9ay {
class Reader {
    const std::string_view file_name;
    const std::string_view raw;
    int cnt = 0;
    std::pair<int, int> locate(int cnt) {
        int l = 0;
        int c = 0;

        for (int i = 0; i < cnt; i++) {
            if (raw[i] == '\n') {
                l++;
                c = 0;
            }
            else {
                c++;
            }
        }

        return {l, c};
    }

public:
    Reader(const std::string_view _file, std::string_view _raw) : file_name(_file), raw(_raw) {}
    Reader(const Reader &other) = default;
    Reader clone() {
        return Reader(*this);
    }
    std::string report_error(const std::string &error_msg) {
        auto [line, column] = locate(cnt);
        auto err_msg = std::format("{}:{}:{} error : {}", file_name, line, column, error_msg);
        std::print("{}", err_msg);
        return err_msg;
    }
    char32_t next_char() {
        assert_c9ay(cnt < raw.size());
        return raw[cnt++];
    }
    char32_t prev_char() {
        return raw[--cnt];
    }
    bool has_next() {
        return cnt < raw.size();
    }
    char32_t peek_prev() {
        return raw[cnt - 1];
    }
    char32_t peek_next() {
        return raw[cnt];
    }
    std::string_view diff(Reader &begin) {
        int sz = (cnt)-begin.cnt;
        return std::string_view(raw.substr(begin.cnt, sz));
    }
    int get_cnt() {
        return cnt;
    }
};
}  // namespace c9ay
