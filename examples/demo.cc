// A runnable tour of hashes.
//
// Build (when this repo is the top-level project):
//   cmake -B build && cmake --build build && ./build/hashes_demo
//
// The one idea worth taking away: every hash here is constexpr, so the hash of a
// string literal is computed by the compiler and can be a `case` label. That lets
// you `switch` on a runtime string by hashing it and matching against case labels
// that are themselves compile-time hashes. The same hash function run at runtime on
// the same bytes returns the identical value, so the dispatch is sound. This demo
// shows the constexpr proof, the string switch, the families on one input, the
// integer mixer, and the jump-consistent hash.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "hashes.hh"          // xxh64, fnv1ah*, djb2h*, mixer, jump_consistent_hash, _fnv1a, _xx
#include "static_string.hh"   // string(...) -> a constexpr static_string

using namespace static_string;

static void rule(const char* title) {
	std::printf("\n\033[1m-- %s --\033[0m\n", title);
}

// --- The string switch, the headline use case ----------------------------------
// fnv1ah32::hash is constexpr, so each `case` label is a hash computed at compile
// time. We hash the runtime argument once and let the switch jump. No string
// comparisons, no chain of strcmp; the dispatch is an integer switch the compiler
// can turn into a jump table.
static const char* method_kind(std::string_view m) {
	switch (fnv1ah32::hash(m)) {
		case fnv1ah32::hash("GET"):    return "safe, idempotent";
		case fnv1ah32::hash("HEAD"):   return "safe, idempotent";
		case fnv1ah32::hash("PUT"):    return "idempotent, not safe";
		case fnv1ah32::hash("DELETE"): return "idempotent, not safe";
		case fnv1ah32::hash("POST"):   return "neither";
		default:                       return "unknown method";
	}
}

int main() {
	std::printf("hashes demo  (every value below is computed by the same constexpr code)\n");

	// --- 1. the hash is computed at compile time -----------------------------
	rule("constexpr: these are computed by the compiler, not at runtime");
	// static_assert forces the compiler to evaluate the hash. If any of these were
	// not a compile-time constant, the program would not build. The values are
	// baked into the binary as plain integers.
	static_assert(fnv1ah32::hash("GET") == 0x96e6be77UL, "fnv1a32(\"GET\") is a constant");
	static_assert(xxh64::hash("hello", 5) != 0, "constexpr xxHash-64 produces a constant");
	static_assert("hello"_xx == xxh64::hash("hello", 5), "the _xx UDL == xxh64 of the same bytes");
	static_assert("GET"_fnv1a == fnv1ah32::hash("GET"), "the _fnv1a UDL == fnv1ah32 of the same bytes");
	constexpr std::uint32_t get_at_compile_time = fnv1ah32::hash("GET");
	std::printf("  fnv1ah32(\"GET\") baked into the binary at compile time -> 0x%08x\n", get_at_compile_time);
	std::printf("  \"hello\"_xx (constexpr xxHash-64 UDL)               -> 0x%016llx\n",
	            static_cast<unsigned long long>("hello"_xx));

	// --- 2. compile-time == runtime on the same bytes ------------------------
	rule("the same function at runtime gives the same value");
	// Build the key at runtime so the compiler cannot fold it; the constexpr path
	// and the runtime path are the same code, so they must agree. This is what
	// makes the string switch sound: the case labels (compile time) match the
	// switch value (runtime).
	std::string built;
	built += 'G'; built += 'E'; built += 'T';
	const std::uint32_t get_at_runtime = fnv1ah32::hash(std::string_view(built));
	std::printf("  fnv1ah32(\"GET\") hashed at runtime                  -> 0x%08x  (%s)\n",
	            get_at_runtime, get_at_runtime == get_at_compile_time ? "matches" : "MISMATCH");

	// --- 3. switch on a hashed string ----------------------------------------
	rule("switch on a hashed string: dispatch without string compares");
	const char* methods[] = {"GET", "HEAD", "PUT", "DELETE", "POST", "TRACE"};
	for (const char* m : methods) {
		std::printf("  %-7s -> 0x%08x  %s\n", m, fnv1ah32::hash(std::string_view(m)), method_kind(m));
	}

	// --- 4. the hash families on one input -----------------------------------
	rule("the families, same input \"hashing\"");
	// Each family exposes the same hash(...) over const char*+len, literals,
	// static_string, and std::string/string_view. Here is one word through all of
	// them, widths and all.
	constexpr auto key = string("hashing");  // a constexpr static_string argument
	std::printf("  fnv1ah16   -> 0x%04x\n",         fnv1ah16::hash(key));
	std::printf("  fnv1ah32   -> 0x%08x\n",         fnv1ah32::hash(key));
	std::printf("  fnv1ah64   -> 0x%016llx\n",      static_cast<unsigned long long>(fnv1ah64::hash(key)));
	std::printf("  fnv1ah32ci -> 0x%08x  (case-insensitive: folds via chars::tolower)\n",
	            fnv1ah32ci::hash(string("HASHING")));
	std::printf("  djb2h32    -> 0x%08x\n",         djb2h32::hash(key));
	std::printf("  djb2h64    -> 0x%016llx\n",      static_cast<unsigned long long>(djb2h64::hash(key)));
	std::printf("  xxh64      -> 0x%016llx  (constexpr xxHash-64)\n",
	            static_cast<unsigned long long>(xxh64::hash(key)));
	// Case-insensitivity is content equality, not a coincidence of widths:
	static_assert(fnv1ah32ci::hash(string("HASHING")) == fnv1ah32::hash(string("hashing")),
	              "fnv1ah32ci folds case before hashing");
	std::printf("  fnv1ah32ci(\"HASHING\") == fnv1ah32(\"hashing\")? %s\n",
	            fnv1ah32ci::hash(string("HASHING")) == fnv1ah32::hash(string("hashing")) ? "yes" : "no");

	// --- 5. the integer mixer ------------------------------------------------
	rule("mixer: integer -> well-distributed integer (width follows the input)");
	// mixer dispatches on the integer width: a 32-bit input gets mix32, a 64-bit
	// input gets mix64. Adjacent inputs scatter.
	for (unsigned i = 0; i < 4; ++i) {
		std::printf("  mix(uint32 %u) -> 0x%08x      mix(uint64 %u) -> 0x%016llx\n",
		            i, mixer::mix(i),
		            i, static_cast<unsigned long long>(mixer::mix(static_cast<unsigned long long>(i))));
	}

	// --- 6. jump-consistent hash ---------------------------------------------
	rule("jump_consistent_hash: a key -> a bucket in [0, num_buckets)");
	// Lamping and Veach's jump-consistent hash. Growing the bucket count moves only
	// a minimal fraction of keys; here is one key landing across cluster sizes, plus
	// the string overload (an inline FNV-1a that skips '/').
	const std::uint64_t k = 0xdeadbeefcafef00dULL;
	std::fputs("  key 0xdeadbeef... across cluster sizes: ", stdout);
	for (int n : {4, 8, 16, 32, 64}) {
		std::printf("%d->%u  ", n, jump_consistent_hash(k, n));
	}
	std::putc('\n', stdout);
	std::printf("  jump_consistent_hash(\"shard/user/42\", 16) -> bucket %u\n",
	            jump_consistent_hash(std::string_view("shard/user/42"), 16));

	std::puts("\ndone.");
	return 0;
}
