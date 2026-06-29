# hashes

A small, header-only **compile-time hashing** library for C++20, extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).

## What it is

One header, `hashes.hh`: `constexpr` hash functions (FNV-1a, djb2, a `constexpr`
xxHash-64, integer mixers, jump-consistent hashing) plus `_fnv1a` / `_xx`
user-defined literals, so you can write a hash-based `switch` over strings whose
case labels are computed at compile time. The hash families take `const char*`
+ len, string literals, `static_string`, and `std::string` / `std::string_view`
through one uniform `hash(...)`.

It builds on two already-extracted standalone libraries —
[`static-string`](https://github.com/Kronuz/static-string) (so the hash families
accept a `static_string` argument) and
[`char-classify`](https://github.com/Kronuz/char-classify) (for `chars::tolower`,
used by the case-insensitive FNV) — pulled in by `FetchContent` at pinned SHAs.
The single *optional* dependency is the runtime xxHash fast path (see below); the
default build doesn't need it.

## Install

CMake with `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  hashes
  GIT_REPOSITORY https://github.com/Kronuz/hashes.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(hashes)

target_link_libraries(your_target PRIVATE hashes::hashes)
```

`hashes` itself `FetchContent`s its two dependencies —
[`static-string`](https://github.com/Kronuz/static-string) and
[`char-classify`](https://github.com/Kronuz/char-classify) — at the exact SHAs it
was extracted against, so you get the whole stack from one `FetchContent_Declare`.
The `hashes` target is a pure `INTERFACE` library: it compiles nothing, requests
`cxx_std_20`, and puts all three headers (`hashes.hh`, `static_string.hh`,
`chars.hh`) on your include path. Then:

```cpp
#include "hashes.hh"  // xxh64, fnv1ah*, djb2h*, mixer, jump_consistent_hash, _fnv1a, _xx
```

Requires C++20. On macOS it builds with AppleClang/libc++, the same toolchain
Xapiand uses. The header keeps its original filename, so a codebase that already
`#include "hashes.hh"` just needs this repo on its include path.

If you already pull `static-string` and `char-classify` into your own build via
`FetchContent`, declare them (and `hashes`) so the names dedup — CMake's
`FetchContent` uses the first declaration of each name, so declare them at the SHAs
you want and `hashes` will reuse them rather than fetching its pinned copies.

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

`fnv1ah16/32/64`, `fnv1ah32ci` (case-insensitive), `djb2h8/16/32/64`, and the
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
  xxHash header rides in with LZ4.

Force the choice with `-DSTRING_KIT_HAS_XXHASH=1` or `=0` if you don't want the
auto-detection.

## Build & test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

The first `cmake -B build` clones `static-string` and `char-classify` from GitHub
at the pinned SHAs in `CMakeLists.txt`. The test checks FNV/djb2/xxh64 values
against known vectors, hashing a `static_string` argument, the integer `mixer` and
`jump_consistent_hash`, and the `_fnv1a`/`_xx` UDL string switch. It prints
`all hashes tests passed` and exits 0.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake build
produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/hashes_demo
```

It opens with a `static_assert` that the FNV-1a of `"GET"` is a compile-time
constant (if it weren't, the program wouldn't build), then hashes the same bytes
*at runtime* and shows the two agree. That agreement is the whole point: it makes
the headline use case sound, a `switch` over a runtime string whose `case` labels
are compile-time hashes, dispatching HTTP methods without a single string compare.
From there it runs one word through every family at every width (FNV-1a 16/32/64,
the case-insensitive `fnv1ah32ci` folding `"HASHING"` to match `"hashing"`, djb2,
and the `constexpr` xxHash-64), shows the integer `mixer` scattering adjacent
inputs, and watches `jump_consistent_hash` place one key across growing bucket
counts plus the string overload that skips `/`. Every value is printed labelled as
`string -> hash`.

## Provenance

Extracted from [Xapiand](https://github.com/Kronuz/Xapiand). The standalone delta
is pure decoupling: `hashes.hh` made its `#include "xxhash.h"` optional behind
`STRING_KIT_HAS_XXHASH` / `__has_include`, so the library carries no hard
third-party dependency, and routes its `static_string` and `chars::tolower` needs
through the two sibling libraries instead of Xapiand's in-tree headers. The hashing
logic is otherwise identical. See [ARCHITECTURE.md](ARCHITECTURE.md) for the design
and [AGENTS.md](AGENTS.md) for the repo map and invariants.

## License

MIT, Copyright (c) 2015-2019 Dubalu LLC. `hashes.hh` carries portions
Copyright (c) 2015 Daniel Kirchner. See [LICENSE](LICENSE) and the per-file header.
