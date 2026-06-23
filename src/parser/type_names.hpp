#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "base/string_hash.hpp"

namespace c9ay::parser::type_names {

using Name_set = std::unordered_set<
    std::string,
    Transparent_string_hash,
    Transparent_string_equal>;

static thread_local std::vector<Name_set> scopes;

inline void reset() {
    scopes.clear();
    scopes.emplace_back();
}

inline void push_scope() {
    if (scopes.empty()) reset();
    scopes.emplace_back();
}

inline void pop_scope() {
    scopes.pop_back();
}

inline bool contains(std::string_view name) {
    for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
        if (scopes[i].contains(name)) return true;
    }
    return false;
}

inline void add(std::string_view name) {
    if (scopes.empty()) reset();
    scopes.back().insert(std::string(name));
}

struct Scope {
    Scope() {
        push_scope();
    }

    ~Scope() {
        pop_scope();
    }
};

}  // namespace c9ay::parser::type_names
