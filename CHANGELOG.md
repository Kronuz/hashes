# Changelog

Notable changes to `hashes`, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-11-01

### Added

- `bench/compare.cc` (`hashes_compare`, `-DHASHES_BUILD_BENCH=ON`) times `xxh64`,
  `wordwise`, `fnv1a64` and `mixer` against xxHash v0.8.4 (`XXH64`, `XXH3_64bits`,
  fetched by this target only) and `absl::Hash` (found when installed). The numbers are
  in `docs/BENCHMARKS.md`. `hashes_compare --check` is the CTest test `hashes_compare`:
  `xxh64::hash` equals `XXH64` for every length from 0 to 130 and for lengths up to
  128 KiB, with seed 0 and random seeds. Before it, `xxh64` had no external oracle.
  `hashes_compare --quality` prints avalanche and duplicate counts.
- `bench/candidate_wordwise8.hh`: a wide-lane hash, timed beside `wordwise` and kept
  out of the library (see `docs/BENCHMARKS.md`).
- CI builds `hashes_compare` and runs the check.
- `docs/API.md` and `AGENTS.md` state that the self-contained `xxh64` stays scalar.

### Changed

- `docs/reviews/cl-1-code.md` and `docs/status/cl-1.md` no longer name local
  worktree and scratch paths of the review that produced them.
