# hashes API reference

## Usage

### Compile-time hashing and string switches

```cpp
#include "hashes.hh"

// Hash a runtime string, dispatch on compile-time case labels:
int classify(std::string_view s) {
    switch (fnv1ah32::hash(s)) {
        case fnv1ah32::hash("get", 3):    return 1;
        case fnv1ah32::hash("put", 3):    return 2;
        case fnv1ah32::hash("delete", 6): return 3;
        default:                          return 0;
    }
}

constexpr std::uint64_t k = "hello"_xx;          // constexpr xxHash-64
constexpr std::uint32_t g = "get"_fnv1a;         // FNV-1a UDL (see note below)
```

`fnv1ah16/32/64`, `fnv1ah32ci`/`fnv1ah64ci` (case-insensitive), `djb2h8/16/32/64`,
`wordwise`, and the
`constexpr` `xxh64` all expose a uniform `hash(...)` over `const char*`+len,
string literals, `static_string`, and `std::string`/`std::string_view`. `mixer`
hashes integers; `jump_consistent_hash` buckets a key into `[0, num_buckets)`.

The `_fnv1a` UDL is declared `uint32_t` while hashing with `fnv1ah64`, so it
carries the *low 32 bits* of the 64-bit FNV-1a — a quirk preserved verbatim from
Xapiand. Call sites may depend on the value, so it isn't "fixed."

## The optional xxHash seam

`hashes.hh` ships a fully self-contained `constexpr` xxHash-64
(`xxh64::hash(p, len)` and the `_xx` UDL) that needs nothing external. Separately,
it offers *runtime* fast paths — `xxh64::hash(std::string_view)` and the whole
`xxh32` class — that hand off to xxHash's `XXH64()` / `XXH32()` when an xxHash
implementation is on your include path.

The header auto-detects this via `__has_include("xxhash.h")` / `<xxhash.h>`:

- **No xxHash present (the default):** the runtime `xxh64` string overload falls
  back to the `constexpr` code; `xxh32` (which has no `constexpr` implementation)
  is simply not defined. No third-party dependency enters.
- **xxHash present:** `xxh64`'s runtime string overloads call `XXH64()` and the
  `xxh32` class is defined over `XXH32()`. This is the path Xapiand uses, where an
  xxHash header rides in with LZ4. The header is included in xxHash's inline mode
  (`XXH_INLINE_ALL`), so no xxHash library is needed at link time.

Force the choice with `-DSTRING_KIT_HAS_XXHASH=1` or `=0` if you don't want the
auto-detection.

