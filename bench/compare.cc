// The hash families against xxHash and, when built with it, absl::Hash.
//
//   hashes_compare [windows]    ns per hash for keys of 4 bytes to 128 KiB, and for 64-bit integers
//   hashes_compare --check      no timing: xxh64 against XXH64, and the candidate's properties
//   hashes_compare --quality    avalanche and duplicate counts of wordwise and the candidate
//
// hashes is compiled with STRING_KIT_HAS_XXHASH=0, so xxh64::hash is its own constexpr code and
// not a call into XXH64. Every timed call hashes a key at one of 4096 offsets of a random
// buffer, one thread, best of N windows.
#define STRING_KIT_HAS_XXHASH 0
#include "hashes.hh"

#include "candidate_wordwise8.hh"

#define XXH_INLINE_ALL
#include "xxhash.h"

#ifdef HASHES_BENCH_ABSL
#include <absl/hash/hash.h>
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using clk = std::chrono::steady_clock;
static volatile unsigned long long sink;

// Calls f(i) in batches until one batch lasts target_ms, then keeps the fastest of `windows`.
template <class F>
static double best_ns(int windows, double target_ms, F f) {
	long long batch = 64;
	for (;;) {
		auto t0 = clk::now();
		unsigned long long acc = 0;
		for (long long i = 0; i < batch; ++i) acc += f(i);
		sink = acc;
		if (std::chrono::duration<double, std::milli>(clk::now() - t0).count() > target_ms) break;
		batch *= 2;
	}
	double best = 1e300;
	for (int w = 0; w < windows; ++w) {
		auto t0 = clk::now();
		unsigned long long acc = 0;
		for (long long i = 0; i < batch; ++i) acc += f(i);
		sink = acc;
		best = std::min(best, std::chrono::duration<double, std::nano>(clk::now() - t0).count() / batch);
	}
	return best;
}

static const std::size_t kLengths[] = {4, 8, 12, 16, 24, 32, 48, 64, 100, 256, 1024, 16384, 131072};

static std::vector<char> random_buffer() {
	std::mt19937_64 g(3);
	std::vector<char> buf(1 << 20);
	for (auto& c : buf) c = static_cast<char>(g());
	return buf;
}

// xxh64::hash must equal XXH64 for every length and seed: the one external oracle the
// constexpr implementation has.
static bool check_xxh64(const std::vector<char>& buf) {
	std::mt19937_64 g(7);
	std::vector<std::size_t> lengths(std::begin(kLengths), std::end(kLengths));
	for (std::size_t n = 0; n <= 130; ++n) lengths.push_back(n);
	for (std::size_t len : lengths) {
		const int reps = len >= 4096 ? 40 : 2000;
		for (int i = 0; i < reps; ++i) {
			const char* p = buf.data() + g() % 4096;
			const std::uint64_t seed = (i % 4 == 0) ? 0 : g();
			if (xxh64::hash(p, len, seed) != XXH64(p, len, seed)) {
				std::printf("MISMATCH: xxh64 and XXH64 differ at length %zu, seed %llu\n", len, static_cast<unsigned long long>(seed));
				return false;
			}
		}
	}
	return true;
}

// Every input byte must reach the hash, at every length: flipping one bit anywhere changes it.
static bool check_candidate_bytes() {
	std::mt19937_64 g(11);
	for (std::size_t len = 0; len <= 600; ++len) {
		// The key sits between 64 guard bytes on each side. Changing the guards must not change the
		// hash, so a read outside the key is caught.
		std::vector<char> guarded(len + 128);
		for (auto& c : guarded) c = static_cast<char>(g());
		const char* p = guarded.data() + 64;
		const std::uint64_t base = candidate::wordwise8(p, len);
		for (std::size_t i = 0; i < 64; ++i) {
			guarded[i] = static_cast<char>(guarded[i] ^ 0x5a);
			guarded[64 + len + i] = static_cast<char>(guarded[64 + len + i] ^ 0xa5);
		}
		if (candidate::wordwise8(p, len) != base) {
			std::printf("MISMATCH: candidate reads outside its key at length %zu\n", len);
			return false;
		}
		for (std::size_t b = 0; b < 8 * len; ++b) {
			guarded[64 + b / 8] = static_cast<char>(guarded[64 + b / 8] ^ (1 << (b % 8)));
			const bool changed = candidate::wordwise8(p, len) != base;
			guarded[64 + b / 8] = static_cast<char>(guarded[64 + b / 8] ^ (1 << (b % 8)));
			if (!changed) {
				std::printf("MISMATCH: bit %zu of a %zu-byte key does not change the candidate\n", b, len);
				return false;
			}
		}
	}
	return true;
}

// Mean number of output bits that flip when one input bit flips; 32 is ideal.
template <class H>
static double avalanche(H hash, std::size_t len, int keys) {
	std::mt19937_64 g(len + 1);
	std::string k(len, '\0');
	double total = 0;
	long long n = 0;
	for (int t = 0; t < keys; ++t) {
		for (auto& c : k) c = static_cast<char>(g());
		const std::uint64_t h0 = hash(k.data(), len);
		for (std::size_t b = 0; b < 8 * len; ++b) {
			k[b / 8] ^= static_cast<char>(1 << (b % 8));
			total += std::popcount(h0 ^ hash(k.data(), len));
			k[b / 8] ^= static_cast<char>(1 << (b % 8));
			++n;
		}
	}
	return total / static_cast<double>(n);
}

template <class H>
static std::size_t duplicates(H hash, int count, bool long_keys) {
	std::vector<std::uint64_t> v;
	v.reserve(count);
	for (int i = 0; i < count; ++i) {
		const std::string s = long_keys ? "user:" + std::to_string(i) + ":profile:session-token-padding-xyz" : "k" + std::to_string(i);
		v.push_back(hash(s.data(), s.size()));
	}
	std::sort(v.begin(), v.end());
	std::size_t dup = 0;
	for (std::size_t i = 1; i < v.size(); ++i) dup += v[i] == v[i - 1];
	return dup;
}

static void quality() {
	auto ww = [](const char* p, std::size_t n) { return wordwise::hash(p, n); };
	auto w8 = [](const char* p, std::size_t n) { return candidate::wordwise8(p, n); };
	std::printf("avalanche: mean output bits flipped by one input bit (32 is ideal)\n%8s %10s %10s\n", "bytes", "wordwise", "candidate");
	for (std::size_t len : {1, 3, 7, 12, 20, 40, 64, 100, 300})
		std::printf("%8zu %10.2f %10.2f\n", len, avalanche(ww, len, 200), avalanche(w8, len, 200));
	const int count = 1 << 21;
	std::printf("duplicate 64-bit values over %d keys (about 1e-7 expected)\n", count);
	std::printf("  \"k\" + counter          wordwise %zu, candidate %zu\n", duplicates(ww, count, false), duplicates(w8, count, false));
	std::printf("  long structured keys   wordwise %zu, candidate %zu\n", duplicates(ww, count, true), duplicates(w8, count, true));
}

int main(int argc, char** argv) {
	const std::vector<char> buf = random_buffer();
	if (argc > 1 && std::string_view(argv[1]) == "--check") {
		if (!check_xxh64(buf) || !check_candidate_bytes()) return 1;
		std::puts("hashes_compare: xxh64 equals XXH64, every byte reaches the candidate");
		return 0;
	}
	if (argc > 1 && std::string_view(argv[1]) == "--quality") {
		quality();
		return 0;
	}
	if (!check_xxh64(buf)) return 1;
	const int windows = argc > 1 ? std::atoi(argv[1]) : 9;
	std::printf("ns per hash, %s\n", __VERSION__);
#ifdef HASHES_BENCH_ABSL
	std::printf("%8s | %8s %8s %8s %8s %8s %9s %8s | %9s %9s %11s\n", "bytes", "xxh64", "XXH64", "XXH3", "absl", "wordwise", "candidate", "fnv1a64", "xxh64/XXH64", "ww/XXH3", "candidate/XXH3");
#else
	std::printf("%8s | %8s %8s %8s %8s %9s %8s | %9s %9s %11s\n", "bytes", "xxh64", "XXH64", "XXH3", "wordwise", "candidate", "fnv1a64", "xxh64/XXH64", "ww/XXH3", "candidate/XXH3");
#endif
	for (std::size_t len : kLengths) {
		const double target_ms = len >= 16384 ? 30 : 12;
		auto at = [&](long long i) { return buf.data() + ((i * 977) & 4095); };
		const double a = best_ns(windows, target_ms, [&](long long i) { return xxh64::hash(at(i), len); });
		const double b = best_ns(windows, target_ms, [&](long long i) { return XXH64(at(i), len, 0); });
		const double c = best_ns(windows, target_ms, [&](long long i) { return XXH3_64bits(at(i), len); });
		const double d = best_ns(windows, target_ms, [&](long long i) { return wordwise::hash(at(i), len); });
		const double w = best_ns(windows, target_ms, [&](long long i) { return candidate::wordwise8(at(i), len); });
		const double e = best_ns(windows, target_ms, [&](long long i) { return fnv1ah64::hash(at(i), len); });
#ifdef HASHES_BENCH_ABSL
		const double f = best_ns(windows, target_ms, [&](long long i) { return absl::Hash<std::string_view>{}(std::string_view(at(i), len)); });
		std::printf("%8zu | %8.2f %8.2f %8.2f %8.2f %8.2f %9.2f %8.2f | %10.2fx %8.2fx %10.2fx\n", len, a, b, c, f, d, w, e, a / b, d / c, w / c);
#else
		std::printf("%8zu | %8.2f %8.2f %8.2f %8.2f %9.2f %8.2f | %10.2fx %8.2fx %10.2fx\n", len, a, b, c, d, w, e, a / b, d / c, w / c);
#endif
	}
	std::vector<std::uint64_t> keys(4096);
	std::mt19937_64 g(5);
	for (auto& k : keys) k = g();
	const double m1 = best_ns(windows, 12, [&](long long i) { return mixer::mix(keys[i & 4095]); });
	const double m2 = best_ns(windows, 12, [&](long long i) { return XXH3_64bits(&keys[i & 4095], 8); });
	const double m3 = best_ns(windows, 12, [&](long long i) { return XXH64(&keys[i & 4095], 8, 0); });
	std::printf("64-bit key: mixer::mix %.2f ns, XXH3_64bits %.2f ns, XXH64 %.2f ns", m1, m2, m3);
#ifdef HASHES_BENCH_ABSL
	const double m4 = best_ns(windows, 12, [&](long long i) { return absl::Hash<std::uint64_t>{}(keys[i & 4095]); });
	std::printf(", absl::Hash %.2f ns", m4);
#endif
	std::puts("");
	return 0;
}
