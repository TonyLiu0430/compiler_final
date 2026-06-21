#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
    bool is_defined = false;
};

struct Expression_info {
    Type_ptr type;
    bool is_lvalue = false;
    const Symbol *symbol = nullptr;
};

struct Semantic_result {
    std::vector<std::unique_ptr<Symbol>> symbols;
    std::unordered_map<const parser::Expression *, Expression_info> expressions;
    std::unordered_map<const parser::Declarator *, const Symbol *> declarators;

    const Expression_info &info(const parser::Expression &expression) const {
        return expressions.at(&expression);
    }

    const Symbol *symbol(const parser::Declarator &declarator) const {
        auto found = declarators.find(&declarator);
        return found == declarators.end() ? nullptr : found->second;
    }
};

}  // namespace c9ay::semantic
