// Which hash to pick for perfect-hash string dispatch, on real key sets.
//
// Server-side dispatch (Xapiand-style) hashes a field or type name to an integer, then looks
// it up in a constexpr perfect hash (Kronuz/constexpr-phf) to get a dense `switch` slot. The
// hash is the caller's choice, and it is the only part that varies between contenders here, so
// this isolates it: for each real set it times the raw hash and the full dispatch (hash +
// phf.find), for the case-sensitive path (fnv1ah32 / fnv1ah64 / wordwise) and the
// case-insensitive one (fnv1ah32ci / fnv1ah64ci / wordwise_ci).
//
//   cmake --build build --target hashes_bench_dispatch && ./build/hashes_bench_dispatch

#include "hashes.hh"
#include "phf.hh"

#include "dispatch_sets.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
volatile std::uint64_t g_sink = 0;

template <class Hasher>
double time_hash(const std::vector<std::string>& work) {
    double best = 1e18;
    for (int t = 0; t < 25; ++t) {
        const auto t0 = clock_type::now();
        std::uint64_t acc = 0;
        for (int r = 0; r < 400; ++r) {
            for (const auto& w : work) acc += Hasher::hash(w);
        }
        const auto t1 = clock_type::now();
        g_sink += acc;
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        best = std::min(best, ns / double(work.size() * 400));
    }
    return best;
}

template <class Hasher, std::size_t N>
double time_dispatch(const std::string_view (&keys)[N], const std::vector<std::string>& work) {
    using H = decltype(Hasher::hash(std::string_view{}));
    phf::phf<H, N> p;
    std::vector<H> hs(N);
    for (std::size_t i = 0; i < N; ++i) hs[i] = Hasher::hash(keys[i]);
    p.assign(hs.data(), N);
    double best = 1e18;
    for (int t = 0; t < 25; ++t) {
        const auto t0 = clock_type::now();
        std::uint64_t acc = 0;
        for (int r = 0; r < 400; ++r) {
            for (const auto& w : work) acc += p.find(Hasher::hash(w));
        }
        const auto t1 = clock_type::now();
        g_sink += acc;
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        best = std::min(best, ns / double(work.size() * 400));
    }
    return best;
}

template <std::size_t N>
std::vector<std::string> mkwork(const std::string_view (&keys)[N]) {
    std::vector<std::string> pool;
    for (auto k : keys) pool.emplace_back(k);
    std::mt19937 rng(7);
    std::vector<std::string> w;
    w.reserve(4096);
    std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
    for (int i = 0; i < 4096; ++i) w.push_back(pool[pick(rng)]);
    return w;
}

template <class H32, class H64, class HW, std::size_t N>
void row(const char* label, const std::string_view (&keys)[N]) {
    const auto w = mkwork(keys);
    std::printf("  %-34s N=%-3zu\n", label, N);
    std::printf("    raw hash:   32-bit %5.2f | 64-bit %5.2f | wordwise %5.2f  ns\n",
                time_hash<H32>(w), time_hash<H64>(w), time_hash<HW>(w));
    std::printf("    + dispatch: 32-bit %5.2f | 64-bit %5.2f | wordwise %5.2f  ns\n",
                time_dispatch<H32>(keys, w), time_dispatch<H64>(keys, w), time_dispatch<HW>(keys, w));
}
}  // namespace

int main() {
    std::printf("Dispatch hash choice on real key sets (ns/op, best of 25)\n\n");
    std::printf("case-sensitive (hh):\n");
    row<fnv1ah32, fnv1ah64, wordwise>("reserved field names", RESERVED_CS);
    row<fnv1ah32, fnv1ah64, wordwise>("field-type names (mixed length)", FIELD_TYPES_CS);
    std::printf("\ncase-insensitive (hhl):\n");
    row<fnv1ah32ci, fnv1ah64ci, wordwise_ci>("serialiser type names", CI_TYPES);
    std::printf("\n(sink %llu)\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
