#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace c9ay::semantic {

struct Type;

using Type_ptr = std::shared_ptr<Type>;

struct Type {
    bool is_const = false;

    Type(bool _is_const = false)
        : is_const(_is_const) {}

    virtual ~Type() = default;

    virtual bool is_integer() const {
        return false;
    }

    virtual bool is_floating() const {
        return false;
    }

    virtual bool is_arithmetic() const {
        return is_integer() || is_floating();
    }

    virtual bool is_scalar() const {
        return is_arithmetic();
    }

    virtual bool is_complete() const = 0;
    virtual Type_ptr decay() const = 0;
    virtual Type_ptr clone() const = 0;
    virtual std::string name() const = 0;
};

struct Primitive_type : Type {
    enum class Category {
        VOID_TYPE,
        INTEGER,
        CHARACTER,
        FLOATING
    };

    std::string type_name;
    Category category;
    int bit_width;
    bool is_signed;
    int rank;
    int storage_size;
    int alignment;

    Primitive_type(
        std::string _type_name,
        Category _category,
        int _bit_width,
        bool _is_signed,
        bool _is_const = false,
        int _rank = 0,
        int _storage_size = 0,
        int _alignment = 0)
        : Type(_is_const),
          type_name(std::move(_type_name)),
          category(_category),
          bit_width(_bit_width),
          is_signed(_is_signed),
          rank(_rank),
          storage_size(
              _storage_size
                  ? _storage_size
                  : (_bit_width + 7) / 8),
          alignment(
              _alignment
                  ? _alignment
                  : storage_size) {}

    bool is_integer() const override {
        return category == Category::INTEGER ||
               category == Category::CHARACTER;
    }

    bool is_character() const {
        return category == Category::CHARACTER;
    }

    bool is_floating() const override {
        return category == Category::FLOATING;
    }

    bool is_void() const {
        return category == Category::VOID_TYPE;
    }

    bool is_complete() const override {
        return !is_void();
    }

    Type_ptr decay() const override {
        return clone();
    }

    Type_ptr clone() const override {
        return std::make_shared<Primitive_type>(*this);
    }

    std::string name() const override {
        return type_name;
    }
};

struct Pointer_type : Type {
    Type_ptr element;

    Pointer_type(
        Type_ptr _element,
        bool _is_const = false)
        : Type(_is_const),
          element(std::move(_element)) {}

    bool is_scalar() const override {
        return true;
    }

    bool is_complete() const override {
        return true;
    }

    Type_ptr decay() const override {
        return clone();
    }

    Type_ptr clone() const override {
        return std::make_shared<Pointer_type>(*this);
    }

    std::string name() const override {
        return element->name() + "*";
    }
};

struct Array_type : Type {
    Type_ptr element;
    std::optional<long long> size;

    Array_type(
        Type_ptr _element,
        std::optional<long long> _size)
        : element(std::move(_element)),
          size(_size) {}

    bool is_complete() const override {
        return size.has_value() &&
               element &&
               element->is_complete();
    }

    Type_ptr decay() const override {
        return std::make_shared<Pointer_type>(element);
    }

    Type_ptr clone() const override {
        return std::make_shared<Array_type>(*this);
    }

    std::string name() const override {
        return element->name() + "[" +
               (size ? std::to_string(*size) : "") +
               "]";
    }
};

struct Function_type : Type {
    Type_ptr return_type;
    std::vector<Type_ptr> parameters;

    Function_type(
        Type_ptr _return_type,
        std::vector<Type_ptr> _parameters)
        : return_type(std::move(_return_type)),
          parameters(std::move(_parameters)) {}

    bool is_complete() const override {
        return false;
    }

    Type_ptr decay() const override {
        return std::make_shared<Pointer_type>(clone());
    }

    Type_ptr clone() const override {
        return std::make_shared<Function_type>(*this);
    }

    std::string name() const override {
        return "function";
    }
};

struct Record_field {
    std::string name;
    Type_ptr type;
};

struct Record_identity {};

struct Record_type : Type {
    std::string record_name;
    std::vector<Record_field> fields;
    std::shared_ptr<Record_identity> identity;
    bool defined = false;

    Record_type(std::string _record_name)
        : record_name(std::move(_record_name)),
          identity(std::make_shared<Record_identity>()) {}

    int field_index(std::string_view name) const {
        for (int i = 0; i < static_cast<int>(fields.size()); i++) {
            if (fields[i].name == name) return i;
        }
        return -1;
    }

    bool is_complete() const override {
        return defined;
    }

    Type_ptr decay() const override {
        return clone();
    }

    Type_ptr clone() const override {
        return std::make_shared<Record_type>(*this);
    }

    std::string name() const override {
        return record_name;
    }
};

template <class T>
T *as_type(const Type_ptr &type) {
    return dynamic_cast<T *>(type.get());
}

template <class T>
const T *as_type(const Type &type) {
    return dynamic_cast<const T *>(&type);
}

inline bool is_void(const Type_ptr &type) {
    auto primitive = as_type<Primitive_type>(type);
    return primitive && primitive->is_void();
}

inline bool is_character(const Type_ptr &type) {
    auto primitive = as_type<Primitive_type>(type);
    return primitive && primitive->is_character();
}

inline Type_ptr make_pointer(Type_ptr element) {
    return std::make_shared<Pointer_type>(std::move(element));
}

inline Type_ptr make_array(
    Type_ptr element,
    std::optional<long long> size) {
    return std::make_shared<Array_type>(
        std::move(element),
        size);
}

inline Type_ptr make_function(
    Type_ptr return_type,
    std::vector<Type_ptr> parameters) {
    return std::make_shared<Function_type>(
        std::move(return_type),
        std::move(parameters));
}

inline Type_ptr qualify(Type_ptr type, bool is_const) {
    if (!is_const || type->is_const) return type;
    type = type->clone();
    type->is_const = true;
    return type;
}

inline bool same_type(const Type_ptr &lhs, const Type_ptr &rhs) {
    if (!lhs || !rhs || lhs->is_const != rhs->is_const) {
        return false;
    }

    auto lhs_primitive = as_type<Primitive_type>(lhs);
    auto rhs_primitive = as_type<Primitive_type>(rhs);
    if (lhs_primitive || rhs_primitive) {
        return lhs_primitive && rhs_primitive &&
               lhs_primitive->category == rhs_primitive->category &&
               lhs_primitive->bit_width == rhs_primitive->bit_width &&
               lhs_primitive->is_signed == rhs_primitive->is_signed;
    }

    auto lhs_pointer = as_type<Pointer_type>(lhs);
    auto rhs_pointer = as_type<Pointer_type>(rhs);
    if (lhs_pointer || rhs_pointer) {
        return lhs_pointer && rhs_pointer &&
               same_type(lhs_pointer->element, rhs_pointer->element);
    }

    auto lhs_array = as_type<Array_type>(lhs);
    auto rhs_array = as_type<Array_type>(rhs);
    if (lhs_array || rhs_array) {
        return lhs_array && rhs_array &&
               lhs_array->size == rhs_array->size &&
               same_type(lhs_array->element, rhs_array->element);
    }

    auto lhs_function = as_type<Function_type>(lhs);
    auto rhs_function = as_type<Function_type>(rhs);
    if (lhs_function || rhs_function) {
        if (!lhs_function || !rhs_function ||
            !same_type(
                lhs_function->return_type,
                rhs_function->return_type) ||
            lhs_function->parameters.size() !=
                rhs_function->parameters.size()) {
            return false;
        }
        for (int i = 0;
             i < static_cast<int>(lhs_function->parameters.size());
             i++) {
            if (!same_type(
                    lhs_function->parameters[i],
                    rhs_function->parameters[i])) {
                return false;
            }
        }
        return true;
    }

    auto lhs_record = as_type<Record_type>(lhs);
    auto rhs_record = as_type<Record_type>(rhs);
    return lhs_record && rhs_record &&
           lhs_record->identity == rhs_record->identity;
}

}  // namespace c9ay::semantic
