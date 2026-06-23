# TODO

## Reflection dispatch performance

The current parser and semantic dispatch scans reflected derived types and
tries `dynamic_cast` until one matches. Its runtime cost is `O(node kinds)` per
dispatch, but the number of kinds is currently small and fixed.

Do not replace it without benchmarks. Compile time is already expensive, and a
more elaborate reflection/template implementation may be a net loss.

Candidate replacement:

- obtain the dynamic type with `typeid(node)`;
- generate trampoline entries from reflected derived types;
- store entries in a contiguous fixed-size `std::array`;
- initialize an open-addressed flat hash table once using
  `type_info::hash_code()`;
- dispatch through the resulting function pointer;
- do not use `std::unordered_map`;
- do not add `Node_kind`, CRTP, type functions, or factory-only construction.

GCC 16 currently supports constant evaluation of `&typeid(T)` and
`type_info::operator==`, but `type_info::hash_code()` is runtime-only.

Before implementation, benchmark:

1. the current reflected `dynamic_cast` chain;
2. a flat RTTI hash table;
3. common node distributions rather than uniform random types;
4. runtime improvement against additional clean-build time.
