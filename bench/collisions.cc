// Short-key collision counter for the library's 32-bit outputs and 64-to-32 reductions.
//
// For each key set and each reduction it counts the keys whose 32-bit value was already seen
// and prints the ratio to the birthday expectation n - m(1 - exp(-n/m)), m = 2^32. A sound
// reduction sits near 1.00. Uses a 512 MiB bitmap; the full run takes minutes.
//   ./hashes_bench_collisions [set-name-substring]
// The *_ci rows run only on sets without case-variant duplicates.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include "hashes.hh"

using u32 = uint32_t; using u64 = uint64_t;
struct Fn { const char* name; std::function<u32(const char*, size_t)> f; bool ci = false; };
static std::vector<Fn> fns = {
	{"wordwise.lo32", [](const char* p, size_t n){ return (u32)wordwise::hash(p, n); }},
	{"wordwise.hi32", [](const char* p, size_t n){ return (u32)(wordwise::hash(p, n) >> 32); }},
	{"wordwise.xor", [](const char* p, size_t n){ u64 h = wordwise::hash(p, n); return (u32)(h ^ (h >> 32)); }},
	{"wordwise_ci.lo32", [](const char* p, size_t n){ return (u32)wordwise_ci::hash(p, n); }, true},
	{"wordwise_ci.xor", [](const char* p, size_t n){ u64 h = wordwise_ci::hash(p, n); return (u32)(h ^ (h >> 32)); }, true},
	{"wordwise_ci.hi32", [](const char* p, size_t n){ return (u32)(wordwise_ci::hash(p, n) >> 32); }, true},
	{"wordwise::hash32", [](const char* p, size_t n){ return wordwise::hash32(p, n); }},
	{"wordwise_ci::hash32", [](const char* p, size_t n){ return wordwise_ci::hash32(p, n); }, true},
	{"xxh64.hi32", [](const char* p, size_t n){ return (u32)(xxh64::hash(p, n) >> 32); }},
	{"xxh64.xor", [](const char* p, size_t n){ u64 h = xxh64::hash(p, n); return (u32)(h ^ (h >> 32)); }},
	{"fnv1a64.hi32", [](const char* p, size_t n){ return (u32)(fnv1ah64::hash(p, n) >> 32); }},
	{"xxh64.lo32", [](const char* p, size_t n){ return (u32)xxh64::hash(p, n); }},
	{"fnv1a64.lo32", [](const char* p, size_t n){ return (u32)fnv1ah64::hash(p, n); }},
	{"fnv1a64.xor", [](const char* p, size_t n){ u64 h = fnv1ah64::hash(p, n); return (u32)(h ^ (h >> 32)); }},
	{"fnv1a32", [](const char* p, size_t n){ return fnv1ah32::hash(p, n); }},
	{"fnv1a32ci", [](const char* p, size_t n){ return fnv1ah32ci::hash(p, n); }, true},
	{"djb2h32", [](const char* p, size_t n){ return djb2h32::hash(p, n); }},
#if STRING_KIT_HAS_XXHASH
	{"XXH32", [](const char* p, size_t n){ return xxh32::hash(std::string_view(p, n)); }},
#endif
};
using Emit = const std::function<void(const char*, size_t)>&;
struct Set { const char* name; std::function<void(Emit)> gen; bool ci_ok = false; };

static void words(const char* path, size_t maxlen, Emit e) {
	std::ifstream in(path); std::string w;
	while (std::getline(in, w)) if (!w.empty() && w.size() <= maxlen) e(w.data(), w.size());
}
static void allbytes(int len, Emit e) {
	char b[4]; u64 total = 1ULL << (8 * len);
	for (u64 i = 0; i < total; ++i) { for (int k = 0; k < len; ++k) b[k] = (char)(i >> (8*k)); e(b, len); }
}
// all strings of length len over [lo, lo+span)
static void alpha(int len, int lo, int span, Emit e) {
	char b[16]; std::vector<int> d(len, 0);
	for (;;) {
		for (int k = 0; k < len; ++k) b[k] = (char)(lo + d[k]);
		e(b, len);
		int k = len - 1; while (k >= 0 && ++d[k] == span) d[k--] = 0;
		if (k < 0) break;
	}
}
static void decimals(int maxlen, Emit e) { // "0".."99999999"
	char b[16]; u64 lim = 1; for (int i = 0; i < maxlen; ++i) lim *= 10;
	for (u64 v = 0; v < lim; ++v) { int n = snprintf(b, sizeof b, "%llu", (unsigned long long)v); e(b, n); }
}
int main(int argc, char** argv) {
	std::vector<Set> sets = {
		{"all 1-3 byte keys", [](Emit e){ allbytes(1,e); allbytes(2,e); allbytes(3,e); }},
		{"4B printable ASCII (95^4)", [](Emit e){ alpha(4, 0x20, 95, e); }},
		{"5B lowercase (26^5)", [](Emit e){ alpha(5, 'a', 26, e); }},
		{"6B lowercase (26^6)", [](Emit e){ alpha(6, 'a', 26, e); }},
		{"decimal ints 0..99,999,999", [](Emit e){ decimals(8, e); }},
		{"web2 words <=8B", [](Emit e){ words("/usr/share/dict/web2", 8, e); }},
		{"web2 words all", [](Emit e){ words("/usr/share/dict/web2", 99, e); }},
		{"web2 lowercase words <=8B", [](Emit e){ words("/usr/share/dict/web2", 8, [&](const char* p, size_t n){ for (size_t i = 0; i < n; ++i) if (p[i] < 'a' || p[i] > 'z') return; e(p, n); }); }, true},
		{"Q+5 digits (6B)", [](Emit e){ char b[8]; for (int v = 0; v < 100000; ++v) { int n = snprintf(b, sizeof b, "Q%05d", v); e(b, n); } }, true},
		{"k+7 digits (8B)", [](Emit e){ char b[16]; for (int v = 0; v < 10000000; ++v) { int n = snprintf(b, sizeof b, "k%07d", v); e(b, n); } }, true},
		{"doc-+8 digits (12B)", [](Emit e){ char b[24]; for (int v = 0; v < 100000000; ++v) { int n = snprintf(b, sizeof b, "doc-%08d", v); e(b, n); } }, true},
		{"16 hex digits, counter (16B)", [](Emit e){ char b[24]; for (u64 v = 0; v < 100000000; ++v) { int n = snprintf(b, sizeof b, "%016llx", (unsigned long long)v); e(b, n); } }, true},
	};
	const char* only = argc > 1 ? argv[1] : nullptr;
	std::vector<uint64_t> bits(1ULL << 26);
	for (auto& s : sets) {
		if (s.ci_ok == false && !strstr(s.name, "1-3") && !strstr(s.name, "ASCII") && !strstr(s.name, "web2 words") && !strstr(s.name, "decimal")) s.ci_ok = true;
		if (only && !strstr(s.name, only)) continue;
		u64 n = 0; s.gen([&](const char*, size_t){ ++n; });
		double m = 4294967296.0;
		double expc = (double)n - m * (1.0 - std::exp(-(double)n / m));
		printf("\n## %s  n=%llu  expected collisions=%.1f\n", s.name, (unsigned long long)n, expc);
		for (auto& fn : fns) {
			if (fn.ci && !s.ci_ok) continue;
			std::fill(bits.begin(), bits.end(), 0);
			u64 coll = 0;
			s.gen([&](const char* p, size_t l){
				u32 h = fn.f(p, l); u64& w = bits[h >> 6]; u64 bit = 1ULL << (h & 63);
				coll += (w & bit) != 0; w |= bit; });
			printf("%-18s collisions=%-12llu ratio=%.2f\n", fn.name, (unsigned long long)coll, expc > 0 ? coll / expc : 0.0);
			fflush(stdout);
		}
	}
}
