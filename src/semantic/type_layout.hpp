#pragma once

#include <algorithm>
#include <optional>

#include "semantic/type.hpp"

namespace c9ay::semantic {

class Type_layout {
    long long pointer_size = 8;
    long long pointer_alignment = 8;

    static long long align_up(
        long long value,
        long long alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

public:
    std::optional<long long> alignment_of(
        const Type_ptr &type) const {
        if (auto primitive = as_type<Primitive_type>(type)) {
            if (primitive->is_void()) return std::nullopt;
            return std::max<long long>(
                1,
                primitive->bit_width / 8);
        }
        if (as_type<Pointer_type>(type)) {
            return pointer_alignment;
        }
        if (auto array = as_type<Array_type>(type)) {
            return alignment_of(array->element);
        }
        if (auto record = as_type<Record_type>(type)) {
            if (!record->defined) return std::nullopt;
            long long result = 1;
            for (auto &field : record->fields) {
                auto alignment = alignment_of(field.type);
                if (!alignment) return std::nullopt;
                result = std::max(result, *alignment);
            }
            return result;
        }
        return std::nullopt;
    }

    std::optional<long long> size_of(
        const Type_ptr &type) const {
        if (auto primitive = as_type<Primitive_type>(type)) {
            if (primitive->is_void()) return std::nullopt;
            return std::max<long long>(
                1,
                primitive->bit_width / 8);
        }
        if (as_type<Pointer_type>(type)) {
            return pointer_size;
        }
        if (auto array = as_type<Array_type>(type)) {
            if (!array->size) return std::nullopt;
            auto element_size = size_of(array->element);
            if (!element_size) return std::nullopt;
            return *element_size * *array->size;
        }
        if (auto record = as_type<Record_type>(type)) {
            if (!record->defined) return std::nullopt;
            long long result = 0;
            for (auto &field : record->fields) {
                auto alignment = alignment_of(field.type);
                auto size = size_of(field.type);
                if (!alignment || !size) return std::nullopt;
                result = align_up(result, *alignment);
                result += *size;
            }
            auto alignment = alignment_of(type);
            if (!alignment) return std::nullopt;
            return align_up(result, *alignment);
        }
        return std::nullopt;
    }
};

}  // namespace c9ay::semantic
