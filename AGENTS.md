# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
hashes.hh                    constexpr FNV-1a, djb2, xxHash-64, mixer, jump_consistent_hash + _fnv1a/_xx UDLs. Header.
test/test.cc                 Runnable smoke test: hash values, static_string hashing, mixer/jump, _fnv1a/_xx string switch.
CMakeLists.txt               INTERFACE library `hashes` (+ alias hashes::hashes); FetchContents static-string + char-classify; CTest test `hashes`.
LICENSE                      MIT, Copyright (c) 2015-2019 Dubalu LLC (+ Daniel Kirchner, in the header).
README.md                    What it is, install, usage, the optional xxHash seam.
ARCHITECTURE.md              Internal design of each hash family, the xxHash seam, trade-offs.
```

`hashes.hh` is the only header here; everything else is header-only too (only the
test is compiled). The two dependencies —
[`static-string`](https://github.com/Kronuz/static-string) (so the hash families
accept a `static_string` argument) and
[`char-classify`](https://github.com/Kronuz/char-classify) (for `chars::tolower`,
used by the case-insensitive FNV) — are pulled in by `FetchContent` at pinned SHAs
and provide `static_string.hh` and `chars.hh` on the include path.

## Build and run the test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

The first `cmake -B build` clones `static-string` and `char-classify` from GitHub
at the pinned SHAs in `CMakeLists.txt`. Expected output ends with
`all hashes tests passed`, exit 0. The CMake `hashes` target is an `INTERFACE`
library that requests `cxx_std_20`, links `static_string::static_string` and
`char_classify::char_classify`, and exposes the source dir as an include. The test
target is `hashes_test`; the registered CTest name is `hashes`.

To build against local checkouts of the deps instead of cloning, pass:

```sh
cmake -B build \
  -DFETCHCONTENT_SOURCE_DIR_STATIC_STRING=/path/to/static-string \
  -DFETCHCONTENT_SOURCE_DIR_CHAR_CLASSIFY=/path/to/char-classify
```

## Conventions

- **C++20.** Required. Don't drop the target below it.
- **The only includes are the C++ standard library and the two deps.** `hashes.hh`
  does `#include "static_string.hh"` and `#include "chars.hh"`; those resolve via
  the FetchContent'd libraries. Do not add Xapiand headers back. The one *optional*
  external is xxHash, opt-in behind `__has_include` — keep it that way.
- **Filename is stable.** The header keeps its original Xapiand name (`hashes.hh`)
  so a consumer that already `#include`s it just needs this repo on the include
  path. Don't rename it. The two deps also keep their original filenames
  (`static_string.hh`, `chars.hh`) for the same reason — don't rename them in the
  `#include` lines.
- Tabs for indentation, double quotes in code, no em dashes in prose.

## Load-bearing invariants

- **The two dep includes must keep resolving.** `hashes.hh` relies on
  `static_string.hh` (for the `static_string` hash overloads) and `chars.hh` (for
  `chars::tolower` in `fnv1ah32ci`). The `CMakeLists.txt` FetchContents both and
  links their alias targets so the headers are on the include path. If you bump a
  dep SHA, keep both the `FetchContent_Declare` and the `target_link_libraries`
  line.
- **`hashes.hh` must stay xxHash-free by default.** Its `#include "xxhash.h"` is
  gated behind `STRING_KIT_HAS_XXHASH`, which auto-detects via
  `__has_include("xxhash.h")` / `<xxhash.h>` and defaults to off. When off: the
  `constexpr` `xxh64` is fully available, `xxh64::hash(string_view)` routes through
  the `constexpr` implementation, and `xxh32` is **not defined** (it has no
  `constexpr` form — it only ever wrapped `XXH32()`). When on:
  `xxh64::hash(string/string_view)` call `XXH64()` and `xxh32` is defined over
  `XXH32()`. Both modes are exercised; don't collapse the `#if`.
- **The `constexpr` `xxh64::hash(p, len)` core is self-contained — never make it
  call `XXH64()`.** Only the runtime string/string_view overloads may. The `_xx`
  UDL and any `static_assert`-able hash depend on the core staying `constexpr`.
- **The `_fnv1a` UDL truncates to 32 bits on purpose (inherited).** It is declared
  `operator"" _fnv1a(...) -> std::uint32_t` while hashing with `fnv1ah64`, so it
  carries the low 32 bits of the 64-bit FNV-1a. That's a Xapiand quirk preserved
  verbatim; the test asserts exactly that (`== static_cast<uint32_t>(fnv1ah64::
  hash(...))`). Don't "fix" it silently — call sites may depend on the value.

## How to extend

- **Add a hash family.** Follow `fnv1ah` / `djb2h`: a template parameterized on
  the key type and constants, with the uniform `hash(...)` overload set
  (`const char*`+len, literal, `static_string`, `string`/`string_view`) and an
  `operator()`. Keep the core `constexpr`.
- **Always extend the smoke test.** `test/test.cc` is the only executable check.
  Prefer `static_assert` for anything that is `constexpr` (most of the library),
  so a regression is a compile error, not a silent runtime pass.

## Traps

- **Don't add `#include "xxhash.h"` unconditionally.** It would break the
  default-dependency-free promise and fail to compile anywhere xxHash isn't on the
  path. The `__has_include` gate is the whole point.
- **Don't assume `xxh32` exists.** It is only defined when an xxHash backend is
  detected. Code (and tests) that must run without xxHash can't reference it.
- **Don't route the `constexpr` hash core through the runtime path.** A
  `static_assert(xxh64::hash("x", 1) == ...)` must stay evaluable at compile time.
- **A consumer that also FetchContents the deps should declare them first.**
  `FetchContent` uses the first declaration of each name, so if Xapiand (or any
  consumer) declares `static_string` / `char_classify` before pulling `hashes`,
  those win and dedup — make sure the SHAs are compatible.

## Standalone vs. Xapiand

This is a standalone extraction from
[Xapiand](https://github.com/Kronuz/Xapiand). The delta from the original is pure
decoupling:

- `hashes.hh` made `#include "xxhash.h"` optional behind `STRING_KIT_HAS_XXHASH` /
  `__has_include`, and split the `xxh32` class and `xxh64`'s `XXH64`/`XXH32`
  runtime overloads behind that guard. The `constexpr` xxh64, FNV, djb2, mixer,
  jump-consistent hash, and the UDLs are unchanged.
- Its `static_string.hh` and `chars.hh` includes now resolve to the two sibling
  libraries (FetchContent'd) instead of Xapiand's in-tree headers.

Keep extraction hygiene separate from behavior changes so they can be reconciled
with upstream.
