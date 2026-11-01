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
- **Long keys at full speed.** Above 48 bytes `wordwise` is behind XXH3 on both machines,
  and the `constexpr` `xxh64` stays scalar.

## ⚡ Performance

ns for one hash call on a key of the given length in bytes, one thread, lower is faster. `xxh64` is this library's `constexpr` XXH64, built without xxHash; `XXH64` and `XXH3` are `XXH64()` and `XXH3_64bits()` from [xxHash](https://github.com/Cyan4973/xxHash) v0.8.4, fetched by the bench; `absl::Hash` is `absl::Hash<std::string_view>` from [abseil](https://abseil.io) 20260817.0, a Homebrew install found by CMake; `wordwise` is the library's own hash for short keys, which reads eight bytes per step and mixes each with a multiply and a shift.

| machine, bytes | `xxh64` | `XXH64` | `XXH3` | `absl::Hash` | `wordwise` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Apple M4 Pro, 4 | 1.28 | 1.08 | 1.28 | 0.54 | 0.74 |
| Apple M4 Pro, 16 | 1.74 | 1.71 | 1.29 | 0.50 | 1.09 |
| Apple M4 Pro, 64 | 4.74 | 4.72 | 2.62 | 1.29 | 3.38 |
| Apple M4 Pro, 1024 | 41.24 | 41.09 | 21.30 | 7.94 | 148 |
| Apple M4 Pro, 131072 | 5474 | 5422 | 2808 | 2137 | 25141 |
| Intel Core i9-9980HK, 4 | 2.73 | 2.39 | 2.87 | 0.97 | 1.92 |
| Intel Core i9-9980HK, 16 | 4.17 | 3.69 | 2.93 | 0.97 | 2.44 |
| Intel Core i9-9980HK, 64 | 9.65 | 8.99 | 4.51 | 4.62 | 6.90 |
| Intel Core i9-9980HK, 1024 | 67.57 | 66.58 | 45.13 | 39.49 | 177 |
| Intel Core i9-9980HK, 131072 | 8096 | 8085 | 5085 | 5002 | 23769 |

`xxh64` returns the same value as `XXH64` for every length the bench times and every length from 0 to 130, at random offsets and seeds (`hashes_compare --check`, also the CTest test `hashes_compare`), so those two columns time one function; the `constexpr` code takes up to 1.19x XXH64's time. `XXH3`, `absl::Hash` and `wordwise` are different hash functions with different values, so their columns compare choices, not implementations of one function. Use `xxh64` when a value must equal XXH64's, and `wordwise` for short keys where only a good 64-bit value matters, at compile time or at run time: it is ahead of `XXH3` at 4, 8 and 16 bytes on both machines and 1.3x behind at 64 bytes on the M4 Pro, 1.5x on the i9, and 9x behind at 128 KiB on the M4 Pro, 4.7x on the i9. `absl::Hash` is faster than `wordwise` at every length and is the fastest column at every length on the M4 Pro and up to 32 bytes on the i9, but it is not `constexpr` and does not promise the same values between runs, so it suits in-memory tables only. For long keys `XXH3` is the better choice than `wordwise`.

Apple M4 Pro (macOS) and Intel Core i9-9980HK (macOS, x86-64), Apple clang 17, `-O3 -DNDEBUG -std=gnu++20` (a CMake Release build). Each cell is the fastest of nine windows, then the lower of two runs. Every row, both machines' full tables and the run-to-run spread are in [docs/BENCHMARKS.md](docs/BENCHMARKS.md). To rebuild and rerun:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DHASHES_BUILD_BENCH=ON
cmake --build build --target hashes_compare
./build/hashes_compare --check    # xxh64 against XXH64, no timing
./build/hashes_compare            # the table
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
