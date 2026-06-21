#pragma once

#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

#include "parser/parser.h"

namespace c9ay::parser::reflect {

template <class Base>
consteval auto derived_types() {
    std::vector<std::meta::info> result;

    for (auto member : std::meta::members_of(
             ^^c9ay::parser,
             std::meta::access_context::current())) {
        if (std::meta::is_type(member) &&
            std::meta::is_base_of_type(^^Base, member) &&
            !std::meta::is_same_type(^^Base, member)) {
            result.push_back(member);
        }
    }

    return std::define_static_array(result);
}

template <class Result, class Base, class Visitor>
Result dispatch(const Base &node, Visitor &&visitor) {
    template for (constexpr auto type : derived_types<Base>()) {
        using Derived = [:type:];
        if (auto derived = dynamic_cast<const Derived *>(&node)) {
            if constexpr (std::is_void_v<Result>) {
                std::forward<Visitor>(visitor)(*derived);
                return;
            }
            else {
                return std::forward<Visitor>(visitor)(*derived);
            }
        }
    }

    if constexpr (std::is_void_v<Result>) {
        std::forward<Visitor>(visitor)(node);
    }
    else {
        return std::forward<Visitor>(visitor)(node);
    }
}

}  // namespace c9ay::parser::reflect
