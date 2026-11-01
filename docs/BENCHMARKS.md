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

`bench/compare.cc` times `xxh64`, `wordwise` and `fnv1a64` beside xxHash v0.8.4 (`XXH64`,
`XXH3_64bits`, fetched by the benchmark target only), `absl::Hash` when abseil is installed,
and `candidate::wordwise8` (below). Each call hashes a key at one of 4096 offsets of a random
buffer, on one thread; each cell is the fastest of two runs of nine windows. `hashes` is
compiled with `STRING_KIT_HAS_XXHASH=0`, so `xxh64` is its own `constexpr` code and not a
call into `XXH64`. Before timing, the benchmark checks `xxh64` against `XXH64` for every
length it times.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DHASHES_BUILD_BENCH=ON
cmake --build build --target hashes_compare && ./build/hashes_compare
./build/hashes_compare --quality      # avalanche and duplicates
```

ns per hash on an Apple M4 Pro (Apple clang 17, `-O3`):

| bytes | xxh64 | XXH64 | XXH3 | absl::Hash | wordwise | candidate | fnv1a64 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 1.29 | 1.10 | 1.28 | 0.55 | 0.76 | 1.56 | 1.01 |
| 8 | 1.28 | 1.25 | 1.27 | 0.54 | 0.81 | 1.36 | 2.04 |
| 12 | 1.78 | 1.57 | 1.29 | 0.50 | 1.40 | 1.36 | 3.09 |
| 16 | 1.74 | 1.72 | 1.27 | 0.51 | 1.08 | 1.37 | 4.72 |
| 24 | 2.30 | 2.24 | 1.79 | 0.85 | 1.39 | 1.76 | 7.85 |
| 32 | 3.59 | 3.60 | 1.78 | 0.85 | 1.75 | 1.78 | 12.03 |
| 48 | 4.81 | 4.86 | 2.61 | 1.28 | 2.51 | 3.34 | 21.70 |
| 64 | 4.69 | 4.68 | 2.62 | 1.29 | 3.38 | 3.33 | 32.35 |
| 100 | 6.30 | 6.28 | 4.46 | 2.04 | 5.71 | 6.97 | 63.20 |
| 256 | 11.83 | 11.77 | 5.92 | 2.92 | 22.21 | 10.79 | 212 |
| 1024 | 41.31 | 40.60 | 21.36 | 7.90 | 147 | 31.36 | 987 |
| 16384 | 679 | 674 | 350 | 240 | 3090 | 474 | 16776 |
| 131072 | 5447 | 5458 | 2786 | 2128 | 24946 | 3789 | 132930 |

ns per hash on an Intel Core i9-9980HK (Apple clang 17, `-O3`). Rows from 4 to 100 bytes
vary by up to 20% between runs on this machine:

| bytes | xxh64 | XXH64 | XXH3 | absl::Hash | wordwise | candidate | fnv1a64 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 3.09 | 2.63 | 3.27 | 1.02 | 2.12 | 3.55 | 2.07 |
| 8 | 3.16 | 3.12 | 3.29 | 1.03 | 2.05 | 3.50 | 3.49 |
| 12 | 4.29 | 3.98 | 3.27 | 1.02 | 4.38 | 3.36 | 5.10 |
| 16 | 4.68 | 4.09 | 3.10 | 1.02 | 2.66 | 3.38 | 7.16 |
| 24 | 5.55 | 5.11 | 3.51 | 1.71 | 3.45 | 4.20 | 11.98 |
| 32 | 8.89 | 7.76 | 3.59 | 1.76 | 4.20 | 4.31 | 17.91 |
| 48 | 10.81 | 10.17 | 5.18 | 5.21 | 5.85 | 7.75 | 31.55 |
| 64 | 11.47 | 10.40 | 5.51 | 5.31 | 7.98 | 7.81 | 47.38 |
| 100 | 14.25 | 13.75 | 8.07 | 9.78 | 15.25 | 16.21 | 85.98 |
| 256 | 24.36 | 24.54 | 19.75 | 14.56 | 40.57 | 24.39 | 256 |
| 1024 | 80.09 | 80.27 | 51.94 | 48.72 | 199 | 69.41 | 1096 |
| 16384 | 1191 | 1191 | 782 | 749 | 3563 | 1040 | 18801 |
| 131072 | 9469 | 9281 | 5808 | 5772 | 27450 | 8841 | 146416 |

- **`xxh64` equals XXH64.** The values match for every length and seed tested. The
  `constexpr` code runs within 0 to 17% of `XXH64`: 1.17x at 4 bytes on both machines and 1.15x
  at 32 bytes on the i9, closing above 256 bytes. It computes one 8-byte lane per step and stays
  scalar.
- **`wordwise` is the fastest family of the library from 8 bytes up, and falls behind XXH3 as
  keys grow.** It beats XXH3 at 4, 8, 16 and 24 bytes on both machines, loses at 12 (its 4-byte
  tail is read byte by byte), and from 32 bytes on the M4 Pro (48 on the i9) it is slower. On the
  M4 Pro its time is 0.59x of XXH3's at 4 bytes, 0.85x at 16, 1.29x at 64, 3.75x at 256, 6.9x at
  1 KiB and 9.0x at 128 KiB. On the i9 the ratios are 0.65x, 0.86x, 1.45x, 2.05x, 3.8x and 4.7x.
  It mixes one 8-byte word per dependent multiply, so its time grows with the key. Its values
  are frozen, so a faster hash for long keys is a new family.
- **`absl::Hash` is faster than XXH3 at every length on the M4 Pro.** On the i9 it is faster below
  48 bytes and about level with XXH3 above 1 KiB, and slower at 100 bytes (9.78 against 8.07 ns).
- **`fnv1a64` costs one multiply per byte**: 48x XXH3's time at 128 KiB on the M4 Pro.
- **A 64-bit integer key** takes 0.76 ns through `mixer::mix`, against 1.00 ns for `XXH3_64bits`,
  1.04 ns for `XXH64` and 0.40 ns for `absl::Hash` (M4 Pro); 1.74, 1.92, 2.20 and 0.71 ns on the i9.

### The candidate

`bench/candidate_wordwise8.hh` holds `candidate::wordwise8`, a hash written from the algorithm
and not part of the library. It keeps `wordwise`'s multiplier and step. Up to 64 bytes it reads
the head and the tail with overlapping loads. Above 64 bytes it runs eight independent lanes over
64-byte blocks. It cuts `wordwise`'s time at 1 KiB from 147 to 31 ns on the M4 Pro and from 199 to
69 ns on the i9, and at 128 KiB from 24.9 to 3.8 us on the M4 Pro. Its time stays within 1.0 to
1.8x of XXH3's on the M4 Pro and 1.0 to 2.0x on the i9, so it does not reach XXH3 or `absl::Hash`.
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
