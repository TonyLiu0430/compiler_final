#pragma once
#include <stdexcept>
#include <string>

namespace c9ay {
class internal_error : std::invalid_argument {
public:
    explicit internal_error(const std::string& what_arg) : std::invalid_argument(what_arg) {}
    explicit internal_error(const char* what_arg) : std::invalid_argument(what_arg) {}
};
class invalid_argument : std::invalid_argument {
public:
    explicit invalid_argument(const std::string& what_arg) : std::invalid_argument(what_arg) {}
    explicit invalid_argument(const char* what_arg) : std::invalid_argument(what_arg) {}
};
}  // namespace c9ay