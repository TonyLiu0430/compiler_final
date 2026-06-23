#pragma once

#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

#include "semantic/type.hpp"

namespace c9ay::semantic::reflect {

// TODO(dispatch-performance):
// Keep this in sync with parser/reflect_dispatch.hpp. If profiling justifies
// it, share one reflection-generated fixed flat RTTI table implementation.
// Avoid std::unordered_map and avoid adding type IDs/functions to every type.

consteval auto type_nodes() {
    std::vector<std::meta::info> result;
    for (auto member : std::meta::members_of(
             ^^c9ay::semantic,
             std::meta::access_context::current())) {
        if (std::meta::is_type(member) &&
            std::meta::is_base_of_type(^^Type, member) &&
            !std::meta::is_same_type(^^Type, member)) {
            result.push_back(member);
        }
    }
    return std::define_static_array(result);
}

template <class Result, class Visitor>
Result dispatch(const Type &type, Visitor &&visitor) {
    template for (constexpr auto node : type_nodes()) {
        using Derived = [:node:];
        if (auto derived = dynamic_cast<const Derived *>(&type)) {
            return std::forward<Visitor>(visitor)(*derived);
        }
    }
    return std::forward<Visitor>(visitor)(type);
}

}  // namespace c9ay::semantic::reflect
