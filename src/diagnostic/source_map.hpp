#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace c9ay {

struct Include_site {
    int file;
    int position;
};

struct Source_file {
    std::string name;
    std::string source;
    std::vector<int> line_starts{0};

    Source_file(
        std::string _name,
        std::string _source)
        : name(std::move(_name)),
          source(std::move(_source)) {
        for (int i = 0; i < static_cast<int>(source.size()); i++) {
            if (source[i] == '\n') line_starts.push_back(i + 1);
        }
    }

    std::pair<int, int> locate(int position) const {
        position = std::clamp(
            position,
            0,
            static_cast<int>(source.size()));
        int line = static_cast<int>(
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

    std::string_view line(int index) const {
        int begin = line_starts[index];
        int end = index + 1 < static_cast<int>(line_starts.size())
            ? line_starts[index + 1] - 1
            : static_cast<int>(source.size());
        if (end > begin && source[end - 1] == '\r') end--;
        return std::string_view(source).substr(begin, end - begin);
    }
};

struct Source_mapping {
    int output_begin;
    int output_end;
    int file;
    int source_begin;
    int source_end;
    std::vector<Include_site> include_stack;
};

struct Mapped_source_location {
    const Source_file *file = nullptr;
    int position = 0;
    std::vector<Include_site> include_stack;
};

class Source_map {
    std::vector<Source_file> files;
    std::vector<Source_mapping> mappings;

public:
    int add_file(
        std::string_view name,
        std::string_view source) {
        files.emplace_back(
            std::string(name),
            std::string(source));
        return static_cast<int>(files.size()) - 1;
    }

    const Source_file &file(int index) const {
        return files[index];
    }

    void add_mapping(Source_mapping mapping) {
        if (mapping.output_end <= mapping.output_begin) return;
        mappings.push_back(std::move(mapping));
    }

    Mapped_source_location map(int output_position) const {
        if (mappings.empty()) return {};

        auto found = std::upper_bound(
            mappings.begin(),
            mappings.end(),
            output_position,
            [](int position, const Source_mapping &mapping) {
                return position < mapping.output_begin;
            });
        if (found != mappings.begin()) found--;
        if (output_position < found->output_begin ||
            output_position > found->output_end) {
            return {};
        }

        int output_offset =
            output_position - found->output_begin;
        int source_width =
            found->source_end - found->source_begin;
        return {
            &files[found->file],
            found->source_begin +
                std::min(output_offset, source_width),
            found->include_stack
        };
    }
};

}  // namespace c9ay
