# hashes benchmarks

## Benchmark: which hash for dispatch

`bench/dispatch.cc` answers a practical question: which hash to pick when you use one for
perfect-hash string dispatch (hash a name, look it up in a
[constexpr-phf](https://github.com/Kronuz/constexpr-phf) for a dense `switch` slot). It times
each family on real key sets, both the raw hash and the full dispatch. The word-at-a-time
`wordwise` / `wordwise_ci` win on every real set, on both x86_64 and arm64 (x86_64 dispatch
ns/op shown):

| set | 32-bit FNV | wordwise |
| :-- | ---: | ---: |
| reserved names (cs, ~10 chars) | 9.2 | 6.1 |
| field-type names (cs, 2..23) | 13.9 | 7.9 |
| serialiser type names (ci, short) | 19.6 | 15.7 |

Byte-at-a-time FNV walks one character per step; `wordwise` reads eight, so it pulls ahead as
keys lengthen, and `wordwise_ci` folds case eight bytes at a time (SWAR) where FNV's ci path
does a per-byte table lookup.

```sh
cmake --build build --target hashes_bench_dispatch && ./build/hashes_bench_dispatch
```

The benchmark pulls `constexpr-phf` as a bench-only contender; the library itself has no such
dependency.

## Benchmark: short-key collisions of the 32-bit reductions

`bench/collisions.cc` hashes a key set and counts the keys whose 32-bit value was already seen,
against the birthday expectation `n - m(1 - exp(-n/m))` with `m = 2^32`. A sound reduction
lands near 1.00x. It needs a 512 MiB bitmap and runs for minutes:

```sh
cmake --build build --target hashes_bench_collisions && ./build/hashes_bench_collisions [set]
```

Collisions (ratio to expectation), measured on arm64 macOS; the sets are exhaustive except the
dictionary (`/usr/share/dict/web2`) and the counters:

| key set | keys | expected | wordwise xor-fold | wordwise `hash32` | wordwise low 32 | xxh64 low 32 | fnv1a32 | djb2h32 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| all 1-3 byte keys | 16.8M | 32,982 | 0 | 0 | 31,853 (0.97x) | 32,910 (1.00x) | 0 | 16.5M (502x) |
| 4-byte printable ASCII | 81.5M | 767,464 | 0 | 0 | 769,650 (1.00x) | 766,995 (1.00x) | 2.30M (2.99x) | 78.0M (102x) |
| 5-byte lowercase | 11.9M | 16,419 | 11.4M (696x) | 0 | 16,348 (1.00x) | 16,264 (0.99x) | 180 (0.01x) | 0 |
| 6-byte lowercase | 308.9M | 10.8M | 308.5M (28.4x) | 5.9M (0.55x) | 10.8M (1.00x) | 10.9M (1.00x) | 11.1M (1.02x) | 0 |
| decimal 0..99,999,999 | 100M | 1.16M | 10.9M (9.4x) | 1.15M (1.00x) | 1.16M (1.00x) | 1.16M (1.00x) | 1.18M (1.02x) | 1.25M (1.08x) |
| web2 words, <= 8 bytes | 88,758 | 1 | 21,837 | 1 | 0 | 0 | 0 | 2 |
| `Q` + 5 digits | 100,000 | 1 | 99,000 | 0 | 1 | 1 | 0 | 0 |
| `doc-` + 8 digits | 100M | 1.16M | 16.2M (14.0x) | 1.10M (0.95x) | 1.15M (1.00x) | 1.15M (1.00x) | 1.07M (0.93x) | 1.48M (1.28x) |
| 16 hex digits | 100M | 1.16M | 1.12M (0.97x) | 1.15M (1.00x) | 1.15M (1.00x) | 1.15M (1.00x) | 1.15M (0.99x) | 75,072 (0.06x) |

The xor-fold (`lo ^ hi` of the 64-bit `wordwise`) collapses keys of 5 to 11 bytes that agree in
their first four bytes: `wordwise` ends in `h ^= h >> 32`, so `lo ^ hi` is the low half of the
last multiply, and that half is a function of the low 32 bits of its input. `hash32`, the high
half, is a multiplicative hash of the whole key and stays at or under the birthday expectation
on every set. `djb2h32` collides on short keys by construction (`33^k` spans too few values
below four bytes); `fnv1a32` runs 2 to 3x over on four-byte printable keys.

## Benchmark: against xxHash and absl::Hash

`bench/compare.cc` times `xxh64`, `wordwise` and `fnv1a64` beside xxHash v0.8.4
([Cyan4973/xxHash](https://github.com/Cyan4973/xxHash); `XXH64`, `XXH3_64bits`, fetched at that tag by the
benchmark target only), `absl::Hash<std::string_view>` from [abseil](https://github.com/abseil/abseil-cpp)
20260817.0 (the Homebrew formula `abseil`, installed on both machines and found by CMake when present), and
`candidate::wordwise8` (below). Each call hashes a key at one of 4096 offsets of a random buffer, on one
thread; each cell is the fastest of nine windows, then the lower of two runs. `hashes` is compiled with
`STRING_KIT_HAS_XXHASH=0`, so `xxh64` is its own `constexpr` code and not a call into `XXH64`.

Which columns return the same values: `xxh64` equals `XXH64`. The benchmark checks it before every run,
for each length it times and every length from 0 to 130, at random offsets and with seed 0 and random seeds,
and `hashes_compare --check` runs only that check (CTest runs it as `hashes_compare`). `XXH3`, `absl::Hash`,
`wordwise`, the candidate and `fnv1a64` are different hash functions with different values, so their columns
time other functions. `absl::Hash` is seeded from an address in the process, so its values differ between
runs, and it is not `constexpr`.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DHASHES_BUILD_BENCH=ON
cmake --build build --target hashes_compare
./build/hashes_compare --check        # no timing: xxh64 against XXH64, and the candidate's properties
./build/hashes_compare                # the tables below
./build/hashes_compare --quality      # avalanche and duplicates
```

Both machines build with Apple clang 17 and `-O3 -DNDEBUG -std=gnu++20` (a CMake Release build).

ns per hash on an Apple M4 Pro (macOS); the two runs differ by at most 2% in any cell:

| bytes | xxh64 | XXH64 | XXH3 | absl::Hash | wordwise | candidate | fnv1a64 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 1.28 | 1.08 | 1.28 | 0.54 | 0.74 | 1.53 | 1.02 |
| 8 | 1.28 | 1.26 | 1.28 | 0.54 | 0.81 | 1.37 | 2.05 |
| 12 | 1.78 | 1.57 | 1.27 | 0.50 | 1.39 | 1.38 | 3.27 |
| 16 | 1.74 | 1.71 | 1.29 | 0.50 | 1.09 | 1.38 | 4.77 |
| 24 | 2.30 | 2.25 | 1.80 | 0.85 | 1.42 | 1.77 | 7.86 |
| 32 | 3.60 | 3.61 | 1.79 | 0.85 | 1.72 | 1.77 | 12.06 |
| 48 | 4.81 | 4.91 | 2.61 | 1.28 | 2.50 | 3.32 | 21.91 |
| 64 | 4.74 | 4.72 | 2.62 | 1.29 | 3.38 | 3.34 | 32.25 |
| 100 | 6.34 | 6.27 | 4.45 | 2.03 | 5.74 | 6.95 | 63.05 |
| 256 | 11.95 | 11.86 | 6.00 | 2.84 | 22.18 | 10.83 | 215 |
| 1024 | 41.24 | 41.09 | 21.30 | 7.94 | 148 | 32.00 | 1003 |
| 16384 | 679 | 682 | 350 | 241 | 3114 | 477 | 16823 |
| 131072 | 5474 | 5422 | 2808 | 2137 | 25141 | 3789 | 134115 |

ns per hash on an Intel Core i9-9980HK (macOS, x86-64); the two runs differ by up to 16% in a cell (16% at
16 bytes, 12% at 100 bytes), so differences under about 15% between two cells here are noise:

| bytes | xxh64 | XXH64 | XXH3 | absl::Hash | wordwise | candidate | fnv1a64 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 2.73 | 2.39 | 2.87 | 0.97 | 1.92 | 3.17 | 1.91 |
| 8 | 3.02 | 2.74 | 2.97 | 1.00 | 1.82 | 3.22 | 3.04 |
| 12 | 4.14 | 3.61 | 2.85 | 1.00 | 4.13 | 3.20 | 4.88 |
| 16 | 4.17 | 3.69 | 2.93 | 0.97 | 2.44 | 3.16 | 6.43 |
| 24 | 5.25 | 4.52 | 3.15 | 1.65 | 3.08 | 3.83 | 10.81 |
| 32 | 8.24 | 7.42 | 3.14 | 1.67 | 3.70 | 3.81 | 16.06 |
| 48 | 10.12 | 9.35 | 4.54 | 4.57 | 5.22 | 6.74 | 27.27 |
| 64 | 9.65 | 8.99 | 4.51 | 4.62 | 6.90 | 6.85 | 40.74 |
| 100 | 12.64 | 11.99 | 7.20 | 8.09 | 13.20 | 13.68 | 76.16 |
| 256 | 20.98 | 20.48 | 16.94 | 12.71 | 35.15 | 21.20 | 223 |
| 1024 | 67.57 | 66.58 | 45.13 | 39.49 | 177 | 60.75 | 952 |
| 16384 | 1015 | 993 | 629 | 645 | 3046 | 883 | 16233 |
| 131072 | 8096 | 8085 | 5085 | 5002 | 23769 | 7686 | 128565 |

- **`xxh64` equals XXH64.** The `constexpr` code takes up to 1.19x `XXH64`'s time on the M4 Pro (at 4
  bytes) and 1.16x on the i9 (at 24 bytes), and is level within 2% from 256 bytes up on both. It computes
  one 8-byte lane per step and stays scalar.
- **`wordwise` is the cheapest family of the library up to 100 bytes on the M4 Pro and up to 64 bytes on
  the i9 (level with `fnv1a64` at 4 bytes there), and falls behind XXH3 as keys grow.** On the M4 Pro it
  beats XXH3 at 4, 8, 16, 24, 32 and 48 bytes, loses at 12 (its 4-byte tail is read byte by byte) and is
  slower from 64 bytes; its time is 0.58x of
  XXH3's at 4 bytes, 0.84x at 16, 1.29x at 64, 3.7x at 256, 7.0x at 1 KiB and 9.0x at 128 KiB. On the i9
  it beats XXH3 at 4, 8, 16 and 24 bytes, loses at 12 and from 32 bytes, and the ratios are 0.67x,
  0.83x, 1.53x, 2.1x, 3.9x and 4.7x. It mixes one 8-byte word per dependent multiply, so its time grows
  with the key. Its values are frozen, so a faster hash for long keys is a new family.
- **`absl::Hash` is faster than `wordwise` at every length on both machines.** It is also faster than
  XXH3 at every length on the M4 Pro (0.37 to 0.76 of XXH3's time). On the i9 it is 0.33 to 0.53 of
  XXH3's time up to 32 bytes, level at 48 and 64 bytes, 1.12x at 100 bytes (8.09 against 7.20 ns), and 0.75
  to 1.03x from 256 bytes up.
- **`fnv1a64` costs one multiply per byte**: 48x XXH3's time at 128 KiB on the M4 Pro and 25x on the i9.
- **A 64-bit integer key** takes 0.75 ns through `mixer::mix`, against 1.00 ns for `XXH3_64bits`,
  1.04 ns for `XXH64` and 0.40 ns for `absl::Hash` (M4 Pro); 1.49, 1.65, 1.88 and 0.61 ns on the i9.

### The candidate

`bench/candidate_wordwise8.hh` holds `candidate::wordwise8`, a hash written from the algorithm
and not part of the library. It keeps `wordwise`'s multiplier and step. Up to 64 bytes it reads
the head and the tail with overlapping loads. Above 64 bytes it runs eight independent lanes over
64-byte blocks. It cuts `wordwise`'s time at 1 KiB from 148 to 32 ns on the M4 Pro and from 177 to
61 ns on the i9, and at 128 KiB from 25.1 to 3.8 us on the M4 Pro. Its time stays within 1.0 to
1.8x of XXH3's on the M4 Pro and 1.1 to 1.9x on the i9, so it does not reach XXH3 or `absl::Hash`.
Its values differ from `wordwise`'s, so it would ship as a new family.
`hashes_compare --check` requires every input bit of keys up to 600 bytes to change the hash and
no byte outside the key to matter.

### Quality

`hashes_compare --quality` flips each input bit of 200 random keys per length and counts the
output bits that change, 32 being ideal:

| bytes | 1 | 3 | 7 | 12 | 20 | 40 | 64 | 100 | 300 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: | --: |
| wordwise | 30.04 | 30.18 | 25.79 | 31.36 | 31.58 | 32.01 | 32.00 | 31.92 | 31.97 |
| candidate | 31.69 | 31.94 | 31.99 | 32.01 | 32.01 | 32.03 | 31.99 | 31.99 | 32.00 |

`wordwise` flips 25.8 of 32 bits for a 7-byte key. Over 2,097,152 keys (`"k"` + a counter, and
longer structured keys) neither hash has a duplicate 64-bit value.
