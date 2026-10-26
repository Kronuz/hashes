# fold32: the 32-bit fold collides on short keys

Branch `fix/fold32-collisions`, from origin/main cddbef0.

## State

Confirmed and fixed additively. The library has no fold of its own; the weakness is the xor-fold
of `wordwise`'s 64-bit hash that a 32-bit consumer reaches for (strata hit it in compact-slots:
`Q1000`..`Q1006` share one fold). `wordwise::hash32` / `wordwise_ci::hash32` (the high half) are
added. No existing output changed.

## Findings

1. `wordwise`, `wordwise_ci`: lo ^ hi of the 64-bit value is the low half of the last multiply
   (the final `h ^= h >> 32` cancels), a function of the low 32 bits of `h ^ tail`. Keys under
   eight bytes that share their first four bytes collide wholesale: 5-byte lowercase 11.4M
   collisions in 11.9M keys (696x the birthday expectation), web2 words <= 8 bytes 21,837 of
   88,758, `Q`+5 digits 99,000 of 100,000, 12-byte `doc-`+8 digits 14.0x.
2. `wordwise` low 32 bits and high 32 bits are sound (1.00x or below on every set). `hash32` is
   the high half: 0 collisions on all 1-5 byte exhaustive sets, 0.55x to 1.00x elsewhere.
3. `xxh64` (low, high, xor) and `fnv1ah64` xor-fold: sound. `fnv1ah64` low 32: 2.42x on 4-byte
   printable ASCII, 1.38x on 6-byte lowercase (FNV-1a's low bits mix poorly).
4. `fnv1ah32` (and `_fnv1a`, `fnv1ah32ci`): 2.99x on 4-byte printable ASCII, else ~1.0x.
   `djb2h32`: 502x on all 1-3 byte keys, 102x on 4-byte printable; by construction, not changed.
5. External `XXH32`: 1.94x on 6-byte lowercase (not ours).
6. Docs said `_fnv1a` hashes with `fnv1ah64` and truncates; the code and the test use `fnv1ah32`.
   AGENTS.md, ARCHITECTURE.md and docs/API.md corrected.

Full tables: `docs/BENCHMARKS.md`; harness `bench/collisions.cc` (`hashes_bench_collisions`).

## Callers (who stores or sends a hash)

None of these change: only additions. The list is for the rollout of `hash32`.

1. strata `core/index.hh` `key_hash`: already `wordwise::hash(...) >> 32` (in-memory version
   map); equals `hash32`, so it can call it. No persisted value.
2. strata `core/{keys,reader,writer,section_io}.hh`, `index.hh`: `xxh64::hash` 64-bit, persisted
   in key filters. Unchanged, so on-disk data is safe.
3. prospect `writer.hh:420`: `wordwise::hash` 64-bit, in-memory term table. Unchanged.
4. bloom-filter `bloom_filter.hh`: `xxh64` and `fnv1ah64` 64-bit seeds. Unchanged.
5. Xapiand: `fnv1ah64(term) % n_shards` (`database/handler.cc`, `schema.cc`) is persistent shard
   routing and wire-visible; `xxh64` in `database/utils.h` feeds stored values;
   `fnv1ah32` builds `UUIDRepr` enum values and option switches (in-process);
   `jump_consistent_hash` routes by node count. All unchanged; any change to `fnv1ah64`,
   `xxh64` or `jump_consistent_hash` would break data or placement.
6. `hh`/`hhl`/`hmix` dispatch (Xapiand, enum-reflection, msgpack patcher, constexpr-phf bench):
   64-bit `wordwise` in compile-time tables; not stored.
7. Detent: references hashes in `cmake/dependencies.json` only; no source use found.
8. `djb2h*`, `xxh32`: no user in the Kronuz repos.

Rollout advice (the lead decides): a 32-bit consumer of `wordwise` should call `hash32`; strata
needs no output change. Callers that persist a 32-bit value should keep their current function
unless they hit the collision, since switching changes stored values.

## Done

1. Measured every 32-bit output and reduction on exhaustive short-key sets (bench/collisions.cc).
2. `hash32` on `wordwise` and `wordwise_ci`, all overloads, constexpr.
3. Test: 64-bit outputs pinned; `hash32` overloads; the worst set (117,576 keys): 0 collisions
   (xor-fold: 116,575). Mutants: hash32 as xor-fold fails a static_assert; wordwise final shift
   31 fails the pins. ASan+UBSan: test and demo clean.

## Next

Lead: roll out `hash32` to callers if wanted. CI: see the push note.

## Blocked on

Nothing.
