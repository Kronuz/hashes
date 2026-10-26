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
