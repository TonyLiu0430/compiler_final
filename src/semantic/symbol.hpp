#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "diagnostic/diagnostic.hpp"
#include "parser/node.h"
#include "semantic/type.hpp"

namespace c9ay::semantic {

struct Symbol {
    enum class Kind {
        VARIABLE,
        PARAMETER,
        FUNCTION,
        TYPEDEF_NAME
    };

    Kind kind;
    std::string name;
    Type_ptr type;
    const parser::Node *declaration = nullptr;
    Source_range range;
    bool is_defined = false;
    bool has_static_storage = false;
};

struct Expression_info {
    Type_ptr type;
    bool is_lvalue = false;
    const Symbol *symbol = nullptr;
};

struct Semantic_result {
    Type_ptr void_type;
    Type_ptr character_type;
    Type_ptr integer_type;
    Type_ptr double_type;
    Type_ptr size_type;
    Type_ptr ptrdiff_type;
    std::vector<std::unique_ptr<Symbol>> symbols;
    std::unordered_map<const parser::Expression *, Expression_info> expressions;
    std::unordered_map<const parser::Expression *, long long> constants;
    std::unordered_map<const parser::Declarator *, const Symbol *> declarators;

    const Expression_info &info(const parser::Expression &expression) const {
        return expressions.at(&expression);
    }

    const Symbol *symbol(const parser::Declarator &declarator) const {
        auto found = declarators.find(&declarator);
        return found == declarators.end() ? nullptr : found->second;
    }

    std::optional<long long> constant(
        const parser::Expression &expression) const {
        auto found = constants.find(&expression);
        if (found == constants.end()) return std::nullopt;
        return found->second;
    }
};

}  // namespace c9ay::semantic
