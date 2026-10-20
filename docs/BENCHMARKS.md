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
