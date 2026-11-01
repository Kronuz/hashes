# hashes

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/hashes/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/hashes/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only constexpr string hashing for C++20: hash a runtime string and switch on compile-time case labels.
Every hash family is `constexpr`, so the compiler computes each `case` label and the same
function returns the same value for the runtime input. Reach for it to dispatch on keywords,
commands or HTTP methods without a chain of string compares.

## ✨ Highlights

- **String switches.** `fnv1ah32::hash("GET")` is a constant expression, so it can be a
  `case` label, and `fnv1ah32::hash(s)` on a runtime string returns the same value.
- **Several families, one interface.** FNV-1a (16, 32 and 64 bits, plus case-insensitive
  variants), djb2, a `constexpr` xxHash-64, and `wordwise`, which reads eight bytes per step.
- **Every string form.** `const char*` with a length, string literals, `static_string`,
  `std::string` and `std::string_view`.
- **Literals.** `"get"_fnv1a` and `"hello"_xx` hash a literal where it is written.
- **Integer tools.** `mixer` scrambles integers; `jump_consistent_hash` maps a key to one
  of N buckets.
- **Optional xxHash fast path.** When `xxhash.h` is on the include path, the runtime
  `xxh64` and `xxh32` overloads call `XXH64` and `XXH32`.

## 🚀 Quick start

```cpp
#include "hashes.hh"

#include <cstdio>
#include <string_view>

int method(std::string_view s) {
	switch (fnv1ah32::hash(s)) {
		case fnv1ah32::hash("GET"):    return 1;  // labels hash at compile time
		case fnv1ah32::hash("PUT"):    return 2;
		case fnv1ah32::hash("DELETE"): return 3;
		default:                       return 0;
	}
}

int main() {
	std::printf("%d %d\n", method("PUT"), method("PATCH"));  // 2 0
	static_assert("GET"_fnv1a == fnv1ah32::hash("GET"));
	std::printf("%08x\n", unsigned{"GET"_fnv1a});             // 96e6be77
}
```

[`examples/demo.cc`](examples/demo.cc) runs one word through every family and shows
`jump_consistent_hash` placing a key as the bucket count grows.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer; it fetches
[static-string](https://github.com/Kronuz/static-string) and
[char-classify](https://github.com/Kronuz/char-classify) at pinned commits. CI builds it on
Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan
builds.

```cmake
include(FetchContent)
FetchContent_Declare(hashes
  GIT_REPOSITORY https://github.com/Kronuz/hashes.git
  GIT_TAG        1100eddb0e9f4ce960bd2b69592f13feead16bba)  # or main, to track the tip
FetchContent_MakeAvailable(hashes)

target_link_libraries(my_app PRIVATE hashes::hashes)
```

```cpp
#include "hashes.hh"
```

## 🎯 When to use it

Use it to dispatch on a fixed set of strings, alone or in front of
[constexpr-phf](https://github.com/Kronuz/constexpr-phf) for a dense jump table, and
wherever a hash has to be the same at compile time and at runtime.

Not for:

- **Passwords, signatures or adversarial keys.** None of these hashes is cryptographic.
- **Checksums.** CRC-32C is in [crc32c](https://github.com/Kronuz/crc32c).
- **Long keys at full speed.** Above 32 bytes `wordwise` falls behind XXH3, and the
  `constexpr` `xxh64` stays scalar.

## ⚡ Performance

String-dispatch keys are short, and there `wordwise` is the cheapest family: it hashes 4 to
24 bytes in 0.8 to 1.4 ns, ahead of XXH3 except at 12 bytes. Above 32 bytes its
word-at-a-time loop falls behind XXH3, by 1.3x at 64 bytes and 9x at 128 KiB. The
self-contained `xxh64` runs within 17% of xxHash's `XXH64`. ns per hash, one thread, best
of two runs:

| bytes | `xxh64` | `XXH64` | `XXH3` | `absl::Hash` | `wordwise` |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 1.29 | 1.10 | 1.28 | 0.55 | 0.76 |
| 16 | 1.74 | 1.72 | 1.27 | 0.51 | 1.08 |
| 64 | 4.69 | 4.68 | 2.62 | 1.29 | 3.38 |
| 1024 | 41.3 | 40.6 | 21.4 | 7.9 | 147 |
| 131072 | 5447 | 5458 | 2786 | 2128 | 24946 |

Apple M4 Pro, Apple clang 17 with `-O3`; xxHash v0.8.4 and abseil 20260817. Every row, the
Intel Core i9-9980HK numbers and a wide-lane candidate are in
[docs/BENCHMARKS.md](docs/BENCHMARKS.md). Run it with:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DHASHES_BUILD_BENCH=ON
cmake --build build --target hashes_compare
./build/hashes_compare
```

## 📚 Documentation

- [docs/API.md](docs/API.md): the families, the string switch, the literals and their
  quirks, and the xxHash seam.
- [docs/BENCHMARKS.md](docs/BENCHMARKS.md): which hash wins string dispatch, and the
  families against xxHash and absl.
- [ARCHITECTURE.md](ARCHITECTURE.md): how the families are built.
- [CHANGELOG.md](CHANGELOG.md): dated changes.
- [AGENTS.md](AGENTS.md): repository map, invariants and traps.
- [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries): the catalog.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

Add `-DHASHES_BUILD_BENCH=ON` to build `hashes_compare`, which fetches xxHash and uses
abseil when it is installed.

## 🤝 Contributing

Read [AGENTS.md](AGENTS.md) and the [catalog's rules](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md)
first; hash values are part of the API, so a change keeps every existing value.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and (c) 2015-2020 Dubalu LLC, with portions
Copyright (c) 2015 Daniel Kirchner. See [LICENSE](LICENSE). Extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).
