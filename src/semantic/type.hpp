#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace c9ay::semantic {

struct Type;
using Type_ptr = std::shared_ptr<Type>;

struct Type {
    enum class Kind {
        VOID_TYPE,
        CHAR_TYPE,
        INT_TYPE,
        POINTER_TYPE,
        ARRAY_TYPE,
        FUNCTION_TYPE
    };

    Kind kind;
    bool is_const = false;
    Type_ptr element;
    std::optional<long long> array_size;
    Type_ptr return_type;
    std::vector<Type_ptr> parameters;

    Type(Kind _kind, bool _is_const = false)
        : kind(_kind), is_const(_is_const) {}

    static Type_ptr make_void() {
        return std::make_shared<Type>(Kind::VOID_TYPE);
    }

    static Type_ptr make_char(bool is_const = false) {
        return std::make_shared<Type>(Kind::CHAR_TYPE, is_const);
    }

    static Type_ptr make_int(bool is_const = false) {
        return std::make_shared<Type>(Kind::INT_TYPE, is_const);
    }

    static Type_ptr make_pointer(Type_ptr element) {
        auto type = std::make_shared<Type>(Kind::POINTER_TYPE);
        type->element = std::move(element);
        return type;
    }

    static Type_ptr make_array(
        Type_ptr element,
        std::optional<long long> size) {
        auto type = std::make_shared<Type>(Kind::ARRAY_TYPE);
        type->element = std::move(element);
        type->array_size = size;
        return type;
    }

    static Type_ptr make_function(
        Type_ptr return_type,
        std::vector<Type_ptr> parameters) {
        auto type = std::make_shared<Type>(Kind::FUNCTION_TYPE);
        type->return_type = std::move(return_type);
        type->parameters = std::move(parameters);
        return type;
    }

    bool is_integer() const {
        return kind == Kind::CHAR_TYPE || kind == Kind::INT_TYPE;
    }

    bool is_scalar() const {
        return is_integer() || kind == Kind::POINTER_TYPE;
    }

    bool is_complete() const {
        if (kind == Kind::VOID_TYPE || kind == Kind::FUNCTION_TYPE) return false;
        if (kind == Kind::ARRAY_TYPE) {
            return array_size.has_value() &&
                   element &&
                   element->is_complete();
        }
        return true;
    }

    Type_ptr decay() const {
        if (kind == Kind::ARRAY_TYPE) return make_pointer(element);
        if (kind == Kind::FUNCTION_TYPE) {
            return make_pointer(std::make_shared<Type>(*this));
        }
        return std::make_shared<Type>(*this);
    }

    std::string name() const {
        if (kind == Kind::VOID_TYPE) return "void";
        if (kind == Kind::CHAR_TYPE) return "char";
        if (kind == Kind::INT_TYPE) return "int";
        if (kind == Kind::POINTER_TYPE) return element->name() + "*";
        if (kind == Kind::ARRAY_TYPE) {
            return element->name() + "[" +
                   (array_size ? std::to_string(*array_size) : "") +
                   "]";
        }
        return "function";
    }
};

inline bool same_type(const Type_ptr &lhs, const Type_ptr &rhs) {
    if (!lhs || !rhs || lhs->kind != rhs->kind) return false;
    if (lhs->is_const != rhs->is_const) return false;

    if (lhs->kind == Type::Kind::POINTER_TYPE) {
        return same_type(lhs->element, rhs->element);
    }
    if (lhs->kind == Type::Kind::ARRAY_TYPE) {
        return lhs->array_size == rhs->array_size &&
               same_type(lhs->element, rhs->element);
    }
    if (lhs->kind == Type::Kind::FUNCTION_TYPE) {
        if (!same_type(lhs->return_type, rhs->return_type) ||
            lhs->parameters.size() != rhs->parameters.size()) {
            return false;
        }
        for (int i = 0; i < static_cast<int>(lhs->parameters.size()); i++) {
            if (!same_type(lhs->parameters[i], rhs->parameters[i])) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace c9ay::semantic
