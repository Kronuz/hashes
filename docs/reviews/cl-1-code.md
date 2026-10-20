# CL-1 code review: journal's CRC-32C forwards to `hashes::crc32c`

Reviewer: independent code reviewer. Date: 2026-10-20. Host: Intel i9-9980HK, macOS, Apple clang 17 and Homebrew LLVM 23. A light hold (another agent's measurement) was active throughout, so every build ran at `nice -n 19` with at most 4 jobs.

Branches reviewed

| Repo | Branch | Commits | Worktree |
|---|---|---|---|
| hashes | `cl-1-crc32c-leaf` | 233254b | `~/code/Kronuz/hashes-wt/review-cl1` (detached) |
| journal | `cl-1-crc32c-forward` | 9e9699e (fixtures + test on the byte table), f91a4ae (forward, delete table) | `~/code/Kronuz/journal-wt/review-cl1` (detached) |
| journal | `cl-1-detent-replay` | 9826cf4, 71a8562 (same two commits on Detent's journal pin 57e6250) | `~/code/Kronuz/journal-wt/review-cl1-replay` (detached) |
| cpp-libraries | `cl-1-crc32c-catalog` | b202514 | read only |

Specs: `cpp-libraries/docs/reviews/consolidation-2026-10-19.md` (CL-1, C1-C4) and Detent's CL-1 row in `Detent/docs/CONSOLIDATION_RESPONSE.md`.

## Verdict: APPROVE WITH CONDITIONS

The engine swap is correct and the bytes are identical. The target composition (`HASHES_FAMILIES`) is not acceptable as the contract that journal pins. It should be re-cut before anything pins 233254b, which means before hashes and journal land. A prototype that fixes it is tested below and is small.

Blocking items

1. CL1-1: replace the `HASHES_FAMILIES` switch with an order-insensitive leaf (design A below). Blocking because journal and the Detent replay pin 233254b by SHA, so changing the contract after landing means a second pin round, and because Detent has already said it is blocked on this.

Conditions (non-blocking, before or with landing)

1. CL1-3: make the flipped-byte cases pin the payload CRC checks (one offset and a few added cases).
2. CL1-4: land hashes by fast-forward to the reviewed commit (or re-pin), then journal, then the catalog.
3. CL1-2: have the consumer test inherit the parent's compiler and flags.

## Answers to the five checks

### 1. Byte identity: pass

- Regenerated with the old code. `old-9e9699e` is `git archive` of 9e9699e (byte table still present, `crc_table` found). Its `--generate` wrote a fresh fixture tree. Against the checked-in fixtures: 16 of 16 turn-log segments are byte-identical; `journal-0000000000000001`, `owner.lock` identical. `manifest`, the generation file and the two artifacts differ only in their random identity bytes and the CRCs that cover them (artifact offsets 24-43, generation 24-39 and 56-59, manifest 56-87, 104-119, 132-135); every payload byte and every payload CRC matches.
- Code written by the branch (`--generate` from f91a4ae) against the old-code output: the same 16/16 identical segments and the same differing-offset sets. So old and new engines write the same bytes for the same input.
- The branch's test run against the old-code regenerated tree: `44 checks, 0 failures`.
- The Detent replay fixtures were regenerated at 57e6250 (no turn log exists there). Old-code regeneration at 9826cf4 gives an identical `journal-0000000000000001`, and the replay test passes against it (`40 checks, 0 failures`).
- 64 MiB digest, whole and in 665 random pieces: old table and `hashes::crc32c` both `c649ba9c`; 20000 random offset/length slices agree (`probe_old_vs_new_64mib.cc`).
- The 44 checks verify stored CRCs: the test recomputes every manifest, file-header, batch-header, batch-payload and artifact-ownership CRC with an independent bitwise implementation and requires equality with the stored value (`crc_fixture_test.cc:204-236`). A one-bit polynomial change (`0x82f63b79`) fails (`crc32c equals the bitwise CRC`, then recovery throws `artifact payload checksum mismatch`). A different polynomial (`0xedb88320`) does not even compile (`static_assert` on the vector). The fixtures also verify with the hardware path forced off (`fixture-mut/fallback_only`).
- Refusal of flipped bytes: the baseline refuses every flip I tried (`probe_flip_reasons.cc`, `probe_flip_strength.cc`). But the checked-in flip cases do not pin the individual verify sites (CL1-3).

### 2. The leaf target: configures cleanly, composition is order-sensitive (CL1-1)

- Leaf alone: the `crc32c_consumer` ctest configures a project with `FETCHCONTENT_FULLY_DISCONNECTED ON` and `HASHES_FAMILIES OFF`, links `hashes::crc32c`, and runs. The test is load-bearing: flipping the consumer to `ON` fails it with "FETCHCONTENT_FULLY_DISCONNECTED is set to true, which requires the source" (`.scratch/mut/families-on`). Journal built from git at the pinned SHA fetches no `static-string` or `char-classify` (`build/_deps` holds completion, allocators, metrics, hashes only).
- Order sensitivity, confirmed in two ways (details under CL1-1).

### 3. Hardware against fallback: pass on x86-64, ARM not run

- `probe_hw_vs_table.cc`: x86 SSE4.2 routine, the table routine, the dispatcher, an independent bitwise CRC and the public `Crc32c` class fed in random fragments (including empty pieces, both overloads) agree on 60000 cases: states random plus 0, ~0, 1, 0x80000000, 0x82f63b78; offsets 0-63; lengths 0-70000 with extra mass at 1020-1028, 3068-3076 and 6135-6153 (lane and stride boundaries). Also clean under ASan+UBSan (10000 cases).
- `probe_exact_heap.cc`: exact-size `malloc` blocks, every length 0-7200 at 8 skews, one-shot and two-piece; ASan and UBSan clean, so the hardware loop never reads past `n`.
- Mutants of `crc32c.hh` all fail the hashes `crc32c_test`: lanes swapped, slice-by-8 last byte wrong, byte tail dropped, hardware ignoring the incoming state, empty update resetting state (`.scratch/mut/m/*`). The hardware test is vacuous on a CPU without the instructions (it prints `hardware no` and passes), see CL1-6.
- Empty and fragmented updates: covered by the new tests (`crc32c_test.cc:68-107`) and my probes.

### 4. Nothing else references the deleted table: pass

- Journal review tree: no `crc_table`, no `0x82f63b78` outside `test/crc_fixture_test.cc:58` (the bitwise reference, intended) and prose.
- Consensus (`61149a2` and worktrees) uses only `kronuz::journal::crc32c(...)` (`snapshot.h:60`, `worker.h:787`, tests and fuzz seed); never `Checksum` or the table. The signature is unchanged (`inline std::uint32_t crc32c(std::string_view)`, now `noexcept`), so those sources compile unchanged. `Detent/src|include|tests` have no reference. Strata references are docs only.
- `journal/codec.h` now includes `crc32c.hh` publicly, so every journal consumer needs `hashes::crc32c` on its include path; it does through `journal::journal`'s link interface. Anyone including journal headers by a bare `-I` would break (none found).

### 5. Detent's 205/206 and the segfault: attribution is not established (CL1-5)

See CL1-5. In short: the baseline half of the claim is not in the evidence I can find, and I could not reproduce a segfault on either side.

## Findings

### CL1-1 (blocking): `HASHES_FAMILIES` makes `hashes::hashes` availability depend on who populates `hashes` first

Where: `hashes CMakeLists.txt:22` (the option) and `:34` (families fetched only inside it); `journal CMakeLists.txt:11-12` (declares `hashes` and sets the option OFF); `Detent/cmake/dependencies.json:75-79` (hashes pinned at 7ab0c23, which has no `hashes::crc32c`).

Why it matters: `FetchContent` populates a name once, and the option is read at population time. Whoever populates `hashes` first decides which targets exist for everyone.

Evidence, synthetic (`hashes-wt/review-cl1/.scratch/order/`):

| Order | Result |
|---|---|
| families consumer first, then CRC-only | configures and builds |
| CRC-only first (option OFF), then an unchanged consumer that links `hashes::hashes` | generate step fails: `hashes::hashes ... target was not found` |

Evidence, the real Detent closure (`journal-wt/review-cl1/.scratch/detent-attr/replay-configure.log`): Detent at 3d05a4f with journal replaced by the replay branch fails to configure: `target "journal" contains hashes::crc32c but the target was not found`, because `enum_reflection` (via the http chain) populates `hashes` first at Detent's pin 7ab0c23. After I bumped Detent's `hashes` pin to 233254b in a scratch copy it configured, built and ran, but only because Detent's own `FetchContent_MakeAvailable` order happens to populate the http chain (families ON) before `consensus` pulls journal (families OFF). That is luck, not a property. A future closure with journal before an `enum-reflection`, `bloom-filter`, `msgpack`, `residency` or `strata` consumer breaks, and those consumers (which link `hashes::hashes` and cannot be told to call a function) are unchanged.

Design options considered

- B, a lazy `hashes_enable_families()` function (prototype `.scratch/mut/lazy`): works in both orders for consumers that call the function, but unchanged consumers (`enum_reflection` does `FetchContent_MakeAvailable(constexpr_phf hashes)` and links `hashes::hashes`) cannot call it. Not sufficient.
- D, move CRC-32C to its own repo (`Kronuz/crc32c`), with `hashes` depending on it and keeping a forwarding `crc32c.hh`. Cleanest long-term and order-insensitive, but a new repo, CI and catalog entry for a 200-line header. Reasonable later; not needed to land CL-1.
- A (recommended, tested): make the leaf its own FetchContent unit, in the same repository, under a different FetchContent name, so journal never touches the `hashes` name and there is no option at all.

#### Design A, concrete changes

1. hashes: add `crc32c-leaf/CMakeLists.txt` (full text of the prototype is in `.scratch/proto-a/repo/crc32c-leaf/CMakeLists.txt`):
   ```cmake
   cmake_minimum_required(VERSION 3.14)
   project(hashes_crc32c LANGUAGES CXX)
   if(NOT TARGET hashes::crc32c)
     add_library(hashes_crc32c INTERFACE)
     target_include_directories(hashes_crc32c INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
     target_compile_features(hashes_crc32c INTERFACE cxx_std_20)
     add_library(hashes::crc32c ALIAS hashes_crc32c)
   endif()
   ```
2. hashes root `CMakeLists.txt`: delete the `HASHES_FAMILIES` option and its `if`; define the leaf through the same guarded subdirectory, so `hashes::hashes` links it exactly as today:
   ```cmake
   if(NOT TARGET hashes::crc32c)
     add_subdirectory(crc32c-leaf "${CMAKE_CURRENT_BINARY_DIR}/crc32c-leaf")
   endif()
   ```
   The families block returns to its pre-leaf form (always on), so every existing consumer is unchanged in every order. Re-point `crc32c_test`, `crc32c_bench` at `hashes::crc32c` (already done) and keep `hashes_test`, demo and dispatch bench unconditional.
3. hashes `test/consumer/CMakeLists.txt`: replace the `hashes` declare and `set(HASHES_FAMILIES OFF)` with
   `FetchContent_Declare(hashes_crc32c SOURCE_DIR "${HASHES_SOURCE_DIR}" SOURCE_SUBDIR crc32c-leaf)` and `FetchContent_MakeAvailable(hashes_crc32c)`; keep `FETCHCONTENT_FULLY_DISCONNECTED ON` and the "no hashes::hashes, no static_string" assertion. Pass the parent's compiler and flags (CL1-2). `cmake_minimum_required` in the consumer becomes 3.18 (`SOURCE_SUBDIR`).
4. journal `CMakeLists.txt:11-12` and `:1`: `cmake_minimum_required(VERSION 3.18)`, and
   `FetchContent_Declare(hashes_crc32c GIT_REPOSITORY https://github.com/Kronuz/hashes.git GIT_TAG <leaf commit> SOURCE_SUBDIR crc32c-leaf)`,
   `FetchContent_MakeAvailable(completion allocators hashes_crc32c)`; delete `set(HASHES_FAMILIES OFF)`. Optional: wrap the declare/populate in `if(NOT TARGET hashes::crc32c)` to skip a second clone when the families unit is already present.
5. README and AGENTS (hashes), the catalog entry and journal README: replace the `HASHES_FAMILIES`/first-declaration sentences with "link `hashes::crc32c` by declaring FetchContent name `hashes_crc32c` with `SOURCE_SUBDIR crc32c-leaf`; this never touches the `hashes` name, so order does not matter".
6. Detent: no `hashes` pin bump is required for the closure to configure (see test 6 below), though CL-7 pin alignment still applies.

Tested (prototype commit `.scratch/proto-a/repo` branch `protoA`, via `file://` git so real clone paths are exercised, `.scratch/proto-a/cases/`):

| Case | Result |
|---|---|
| leaf only | builds and runs; `_deps` holds only `hashes_crc32c-src` (no static-string, no char-classify) |
| leaf first, then unmodified `hashes::hashes` consumer | builds, both programs run |
| families first, then leaf | builds, both run |
| `hashes` pinned at old 7ab0c23 (no `hashes::crc32c`), leaf first or last | builds, both run |
| leaf with `FETCHCONTENT_FULLY_DISCONNECTED` and `SOURCE_DIR` + `SOURCE_SUBDIR` | configures, no families target, runs |
| real Detent closure, Detent's unmodified pins (hashes 7ab0c23), journal replay switched to the leaf unit | configures and generates (73 s); no `hashes` pin bump needed |

Residual cost of A, stated honestly: when both units are populated the repository is cloned twice and two copies of `crc32c.hh` exist. That is harmless today because `crc32c.hh` is byte-identical between 7ab0c23 and 233254b (empty `git diff --stat`), and every function in it is `inline` with identical bodies. Keep `crc32c.hh` frozen (or version its namespace) if the two units can ever sit at different commits. The `if(NOT TARGET)` guard in step 4 avoids the second clone when the root is already present.

Should it block landing? Yes, in the sense of "do it before the first pin of the leaf": it changes only hashes' CMake, the journal CMake lines, the consumer test and docs; the header and the engine are unchanged, so everything verified below stays valid and only the pins move. If the owner prefers to land the reviewed state and fix later, the minimum is to document that Detent's closure needs the hashes pin bump and an http-chain-first order, and to expect a second pin round for every consumer; I do not recommend it.

### CL1-2 (low): the consumer test ignores the parent's toolchain

`hashes CMakeLists.txt:67-72` passes only `-DHASHES_SOURCE_DIR`. In my sanitizer build the parent used `/usr/local/opt/llvm/bin/clang++` with ASan/UBSan, but `build-asan/consumer/CMakeCache.txt` shows `/usr/bin/c++` and empty flags. On a CI leg that sets a non-default compiler, or a default compiler without C++20, the consumer fails or tests the wrong toolchain. Pass `-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}`, `-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}`, `-DCMAKE_BUILD_TYPE` (and `--build-config` for multi-config generators).

### CL1-3 (low-medium): the flipped-byte cases do not pin the individual CRC checks

`journal test/crc_fixture_test.cc:243`: for the generation file the offset is `file_header_v2_size + batch_header_size + 2` = 86. The first replayed batch is empty (`batches()[3] == ""`), so offset 86 lands in the second batch's header, not a payload; `probe_flip_reasons.cc` shows it fails with "batch header checksum mismatch". The batch payload check (`journal.h:354`) is therefore never exercised by a flip. Evidence by mutation (`.scratch/fixture_mutants.sh`, copies of the sources with one check removed): with `journal.h:354` removed the 44 checks still pass; likewise for `journal.h:339` (batch header), `:575`/`:584` (artifact), `:712` (manifest, covered by redundant code), and `turn_log.h:186`/`:199` (turn blocks). `probe_flip_strength.cc` (payload-byte flips) shows the first of these is a real gap: with `journal.h:354` removed it reports "ACCEPTED (silently wrong)" for two generation payload flips, and with `turn_log.h:199` removed one turn-log flip at offset 3000 is not reported. The poly mutant is caught, so the engine swap is pinned; what is not pinned is that each verify site still verifies, which this change does not touch. Fix: flip at `file_header_v2_size + 2 * batch_header_size + 2` (batch payload), and flip several payload offsets in a turn-log segment as `probe_flip_strength.cc` does. Cheap; do it with this branch.

### CL1-4 (condition): pins reference an unmerged branch SHA

`journal CMakeLists.txt:11` pins hashes at 233254b, which exists only on `cl-1-crc32c-leaf` (`main` is 7ab0c23). A squash or rebase merge, or deleting the branch, makes that SHA unfetchable and breaks every journal build. Land hashes by fast-forward to the reviewed commit (or re-pin to whatever lands, which under CL1-1 is a new SHA anyway), then journal, then the catalog. The replay branch pins the same SHA. Consumers still pinning hashes 7ab0c23 (strata, residency, msgpack, Xapiand, Detent, bloom-filter) are unaffected until they bump (CL-7).

### CL1-5 (Detent adoption, not a hashes/journal blocker): the 205/206 attribution is not established

What exists: `Detent/.scratch/cl-1-crc32c/` holds `base-configure.log`, `base-build.log`, `configure.log`, `build.log`, `ctest.log`, `logs/new-pins-ctest.log`. The last two are byte-identical copies of one run (205/206, `test_authority_distribution_poll_publisher (SEGFAULT)` after 48.19 s of a 612 s run). There is no `base-ctest.log`, so the "failed 2 of 3 on unmodified Detent" claim has no artifact I can inspect. The Detent build directories are gone.

What I did (`journal-wt/review-cl1/.scratch/detent-attr/`): built only `test_authority_distribution_ownership` (the one binary behind poll_publisher, ownership, idle_100000, idle_500000, held_header and large_scope) from Detent 3d05a4f in two trees, `base` (unmodified pins) and `replay` (journal at 71a8562 plus the hashes pin bump). Then:

1. `ctest -j3` over that group, 3 repetitions each, interleaved: 6 of 6 runs passed in both variants; poll_publisher took 19.3, 20.3, 19.0 s (base) and 18.6, 19.2, 19.3 s (replay).
2. Three concurrent copies of poll_publisher at once, 4 rounds on base and 4 on replay: all 12 copies of each variant fail identically with a thrown `distribution ownership invariant at 382`, exit 1, not a segfault. Line 382 is `check(std::chrono::steady_clock::now() - began < 45s)` in `tests/detent_authority/test_distribution_ownership.cc`, a wall-clock bound on 100 durable (fsync) transactions. So the test is load-sensitive on unmodified Detent as well.
3. No segfault in any run, either variant.

Assessment: the observed segfault at 48.19 s is just past that 45 s deadline, which suggests the crash is on the failure/unwind path after a load-induced `check` throw, not in CRC code. That reading is consistent with the attribution but I could not reproduce the crash on either side, so I cannot confirm it, and I have no stack. The replay changes only the CRC engine (faster, same values) plus a hashes pin, so a data-dependent change is implausible; but "plausible" is not "shown". What closes it: a base (unmodified) `ctest -j3` log of the full suite that shows the same signature, or a core/stack of the segfault. For this review: not a blocker for landing hashes and journal, because nothing in this change can produce different CRC values; it is a condition on Detent's pin bump (Detent should keep the failing log, and the 45 s wall deadline at `test_distribution_ownership.cc:382` is itself worth fixing, since it makes poll_publisher flaky on a busy host regardless of CL-1).

### CL1-6 (info): hardware test is vacuous without the instructions, and ARM is unverified here

`crc32c_test.cc:110` guards the hardware cases with `hardware_available()`; on a runner without SSE4.2/CRC32 it passes while testing only the table. This host is x86-64 only, so the ARMv8 routine (`crc32c.hh:136`) did not run in my review. The CI legs must show `crc32c: hardware yes` on both x86-64 and arm64 (the test prints it). No change needed to the code.

### CL1-7 (info): API widening of `journal::Checksum`

`using Checksum = hashes::Crc32c` (`codec.h`) adds `update(const void*, size_t)` and an explicit seed constructor, and makes `crc32c()` `noexcept` and the update `constexpr`. All source-compatible; I found no caller that the extra overload affects. Fine to land; worth a line in the journal README that `Checksum` is now hashes' type.

### CL1-8 (info): replay branch is faithful

`git diff` of `codec.h` between f91a4ae vs dbf753f and 71a8562 vs 57e6250 is line-identical in its +/- hunks; the replay's CMake differs only in the older completion pin it sits on. Replay Release suite 5/5 (journal, inventory, admission, crc_fixture, completion).

## Suites run

| Suite | Result |
|---|---|
| hashes, Release (`crc32c`, `crc32c_consumer`, `hashes`) | 3/3 passed |
| hashes, ASan+UBSan (Homebrew LLVM 23, Debug) | 3/3 passed |
| hashes `crc32c_test` with libc++ extensive hardening, asserts live, ASan+UBSan | ok |
| journal f91a4ae, Release (9 tests, `-j3`) | 9/9 passed (includes `journal_crc_fixture`, 44 checks, and the 16-segment turn-log power test) |
| journal f91a4ae, RelWithDebInfo, ASan+UBSan, `-fno-sanitize-recover=all` | 9/9 passed |
| journal `crc_fixture_test` with hardening, asserts, ASan+UBSan | 44 checks, 0 failures |
| journal replay 71a8562, Release | 5/5 passed |
| Detent distribution group via `ctest -j3`, base and replay, 3 reps each | 6/6 and 6/6 passed (CL1-5) |

## Probes, mutants and harnesses kept (none deleted)

hashes review worktree, `/Users/kronuz/code/Kronuz/hashes-wt/review-cl1/.scratch/`:
- `probe_hw_vs_table.cc`, `probe_exact_heap.cc`, `probe-bin/`
- `mut/families-on` (consumer test ON mutant), `mut/m/*` (six engine mutants), `mut/lazy` (design B)
- `order/` (four order cases, synthetic), `proto-a/` (design A repo, cases, disconnected consumer)
- logs: `cfg-default.log`, `build-default.log`, `cfg-asan.log`, `build-asan.log`; ASan build in `build-asan/`
- `assert-hard/`

journal review worktree, `/Users/kronuz/code/Kronuz/journal-wt/review-cl1/.scratch/`:
- `fixture_mutants.sh` and `fixture-mut/*` (poly, seven verify-site removals, hashes fallback-only)
- `probe_flip_reasons.cc`, `probe_flip_strength.cc`, `probe_old_vs_new_64mib.cc`, `probe-bin/`
- `old-9e9699e/` (old tree), `regen/` (old-code fixtures), `regen-new/` (new-code fixtures), `assert-hard/`
- `detent-attr/` (`build.sh`, `attr.sh`, `ab.sh`, `ctest_ab.sh`, `base/`, `replay/`, `protoA-cfg/`, logs, `jproto/`, `src` with the hashes pin bump, `src2` with Detent's own pins)
- logs: `ctest-release.log`, `ctest-asan-ubsan.log`

Replay worktree: `/Users/kronuz/code/Kronuz/journal-wt/review-cl1-replay/.scratch/` (`old-9826cf4/`, `regen/`, `old_gen`, `new_on_regen`).

## Not run

- ARMv8 CRC32 routine and the `getauxval` path (no arm64 host); the Linux and FreeBSD gate VMs (`cpp-libraries/tools/preflight.sh`, shared hosts); MSVC.
- TSan; the libFuzzer target (`JOURNAL_FUZZ`); any benchmark, so the audit's 19x and 14-19 GB/s figures are unverified by me.
- Detent's full 206-test suite on base or replay (only the distribution group, CL1-5); the consensus suite against the new journal (consensus uses only the unchanged `crc32c(string_view)` signature, and the CRC identity is shown directly).
- The catalog commit b202514 was read, not built; its claims match the code (one engine, unchanged bytes) but they describe `HASHES_FAMILIES`, so they need the CL1-1 rewrite.
