# Architecture

The internal design of `hashes`: a single header of `constexpr` hash families and
UDLs, built on two sibling libraries. For usage see `README.md`; for the repo map
and invariants see `AGENTS.md`.

## Shape

One header, header-only, with two FetchContent'd dependencies:

```
  static_string.hh   a constexpr string type        (dep: static-string)
  chars.hh           constexpr char classification   (dep: char-classify)
      ▲ (both included by)
  hashes.hh          constexpr hash families + UDLs
```

`hashes.hh` includes `chars.hh` (for `chars::tolower`, used by the
case-insensitive FNV) and `static_string.hh` (so the hash families accept a
`static_string` argument). Both come in through `FetchContent` at pinned SHAs.
Nothing else enters except the C++ standard library and one opt-in exception
(xxHash, below).

## The hash families

A set of hash families, each a template parameterized on the key type and the
algorithm's constants, each exposing the same overload set: `hash(const char*,
len)`, `hash(literal)`, `hash(static_string)`, `hash(string/string_view)`, and an
`operator()`.

- **`fnv1ah<T, prime, offset, Op>`** — FNV-1a, with `fnv1ah16/32/64` and a
  case-insensitive `fnv1ah32ci` (the `Op` folds case via `chars::tolower` before
  hashing — this is the one place the char-classify dependency is load-bearing).
  Fully `constexpr`.
- **`djb2h<T, mul, offset, Op>`** — djb2, with `djb2h8/16/32/64` tuned per width.
  Fully `constexpr`.
- **`xxh64`** — a `constexpr` xxHash-64. The recursive `finalize` / `h32bytes`
  routines reproduce xxHash's mixing in `constexpr` form, so `xxh64::hash("x", 1)`
  is a compile-time constant.
- **`mixer`** — integer-to-integer hashes (32- and 64-bit), dispatched by integer
  width.
- **`jump_consistent_hash`** — Lamping and Veach's jump-consistent hash, mapping a
  key (or a string, via an inline FNV-1a that skips `/`) to `[0, num_buckets)`.

The user-defined literals make compile-time string switches ergonomic: `"get"_xx`
is the `constexpr` xxHash-64 of the bytes, `"get"_fnv1a` the FNV-1a. Used as
`switch`/`case` labels, they let a runtime hash dispatch against compile-time
constants. (`_fnv1a` is declared `uint32_t` while hashing with `fnv1ah64`, so it
carries the low 32 bits — a quirk preserved verbatim from Xapiand.)

## The optional xxHash seam

`constexpr` xxHash-64 is self-contained, but a *runtime* hash of a large
`std::string` is faster through xxHash's optimized `XXH64()`/`XXH32()`. So
`hashes.hh` has one conditional edge: when an xxHash header is reachable
(`__has_include("xxhash.h")` / `<xxhash.h>`, overridable with
`STRING_KIT_HAS_XXHASH`), `xxh64`'s runtime string overloads call `XXH64()` and
the `xxh32` class (which has no `constexpr` form) is defined over `XXH32()`. When
it isn't, the runtime `xxh64` overload falls back to the `constexpr`
implementation and `xxh32` is omitted. The default build detects nothing and
carries no third-party dependency; Xapiand's build sees the xxHash that rides in
with LZ4 and gets the fast path. This is the only place in the library where an
external dependency can enter, and it is opt-in.

## Why this shape

The hash families are the core; the two dependencies are there only because two
overloads reach for them. `fnv1ah32ci` needs a `constexpr` case fold, which is
exactly what `chars::tolower` is, and every family offers a `hash(static_string)`
overload so compile-time strings hash without a conversion. Both were in the same
"constexpr strings" bundle in Xapiand, so pulling them in as siblings keeps the
edges honest — `hashes` declares what it actually uses rather than vendoring
copies that drift. The xxHash fast path is the one thing that didn't belong inside
a header-only library, so it became the opt-in seam rather than a hard include.
Everything else is standard library and `constexpr`.
