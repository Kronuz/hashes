# CL-1: CRC-32C leaves hashes

Consolidation item CL-1 (`cpp-libraries/docs/reviews/consolidation-2026-10-19.md`).
Branch `cl-1-crc32c-extract`, worktree `~/code/Kronuz/hashes-wt/cl-1`, base `main` 7ab0c23.
The code review is `docs/reviews/cl-1-code.md`.

## State

CRC-32C is its own library, [Kronuz/crc32c](https://github.com/Kronuz/crc32c)
(`<crc32c/crc32c.hh>`, `kronuz::crc32c`, target `crc32c::crc32c`, no dependencies), with
the history of `crc32c.hh` from this repository. hashes no longer carries it: `crc32c.hh`,
its test, its benchmark and its docs are gone, and nothing forwards. `hashes::hashes` is
unchanged. Users (journal, strata, residency) migrate directly to `crc32c::crc32c`.

## Why the leaf design was replaced

The first cut kept `hashes::crc32c` inside hashes behind a `HASHES_FAMILIES` option, then
(after review finding CL1-1) behind a second FetchContent name, `hashes_crc32c` with
`SOURCE_SUBDIR crc32c-leaf`. Both worked, but only by making hashes' target set depend on
how a consumer declared it: the option was read when the name `hashes` was first populated,
so population order decided which targets existed; the second name fixed the order
dependence at the price of two clones of one repository and two copies of `crc32c.hh`.
The owner chose to stop special-casing: a checksum that other libraries (journal, strata,
residency) depend on without wanting a string-hash library is a library, so it moved out
and the switch and the second name were never needed. Commits 233254b and 512a20b (on
`cl-1-crc32c-leaf`) are that superseded design and are not part of this branch.

## Done

- Review file committed as its own commit.
- crc32c.hh, test/crc32c_test.cc, bench/crc32c.cc removed; README, ARCHITECTURE, AGENTS,
  docs/API.md, docs/BENCHMARKS.md and the diagram updated. The CRC throughput table moved
  to crc32c's docs/BENCHMARKS.md.
- hashes' suite (`hashes`) passes; a project that takes hashes and crc32c together
  configures and builds in both declaration orders.

## Next

Land crc32c, then this branch, then the consumers (journal, strata, residency), then the
catalog. Detent has no call site of its own (its journal pin brings crc32c in); it bumps
its pins itself.

## Blocked on

Nothing.
