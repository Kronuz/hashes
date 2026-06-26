// Smoke test for the standalone hashes library.
//
// Exercises hashes.hh: FNV-1a / djb2 / constexpr xxh64 produce known values on
// known inputs (constexpr), the integer mixer and jump_consistent_hash, and the
// _fnv1a/_xx UDLs driving a compile-time string switch. The string switch and
// the static_string hash overloads exercise the two FetchContent'd dependencies
// (char-classify for the case-insensitive FNV's tolower, static-string for the
// static_string hash argument).
//
// Build via CMake: cmake -B build && cmake --build build && ctest --test-dir build
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "hashes.hh"
#include "static_string.hh"

using namespace static_string;


// ---------------------------------------------------------------------------
// 1. FNV-1a / djb2 / xxh64 produce known values on known inputs.
// ---------------------------------------------------------------------------

static void test_hash_values() {
	// FNV-1a reference vectors (the canonical 32/64-bit FNV-1a of "" and "a").
	static_assert(fnv1ah32::hash("", 0) == 2166136261UL, "fnv1a32(\"\") == offset basis");
	static_assert(fnv1ah64::hash("", 0) == 14695981039346656037ULL, "fnv1a64(\"\") == offset basis");
	static_assert(fnv1ah32::hash("a", 1) == 0xe40c292cUL, "fnv1a32(\"a\")");
	static_assert(fnv1ah64::hash("a", 1) == 0xaf63dc4c8601ec8cULL, "fnv1a64(\"a\")");

	// Case-insensitive FNV folds case before hashing (this uses chars::tolower
	// from the char-classify dependency).
	static_assert(fnv1ah32ci::hash("ABC", 3) == fnv1ah32::hash("abc", 3),
	              "case-insensitive fnv folds case");
	static_assert(fnv1ah32::hash("ABC", 3) != fnv1ah32::hash("abc", 3),
	              "case-sensitive fnv distinguishes case");

	// djb2-32 of "abc": 5381 -> *33 +'a'/'b'/'c'.
	constexpr std::uint32_t djb_seed = 5381;
	constexpr std::uint32_t djb_abc = (((djb_seed * 33 + 'a') * 33 + 'b') * 33 + 'c');
	static_assert(djb2h32::hash("abc", 3) == djb_abc, "djb2-32(\"abc\")");

	// The constexpr xxh64 is deterministic and content/length-sensitive.
	static_assert(xxh64::hash("abc", 3) == xxh64::hash("abc", 3), "xxh64 is deterministic");
	static_assert(xxh64::hash("abc", 3) != xxh64::hash("abd", 3), "xxh64 distinguishes content");

	// xxh64 also works at runtime over a string_view (no xxHash backend needed:
	// the constexpr path is the fallback when STRING_KIT_HAS_XXHASH is 0).
	std::string abc = "abc";
	assert(xxh64::hash(std::string_view(abc)) == xxh64::hash("abc", 3));

	std::printf("hashes values OK: fnv1a32(\"a\")=0x%08x fnv1a64(\"a\")=0x%016llx djb2-32(\"abc\")=0x%08x\n",
	            fnv1ah32::hash("a", 1),
	            static_cast<unsigned long long>(fnv1ah64::hash("a", 1)),
	            djb2h32::hash("abc", 3));
}


// ---------------------------------------------------------------------------
// 2. Hashing a static_string argument (exercises the static-string dependency).
// ---------------------------------------------------------------------------

static void test_static_string_hash() {
	constexpr auto s = string("get");
	// The static_string overload hashes the same bytes as the const char*+len form.
	static_assert(fnv1ah32::hash(s) == fnv1ah32::hash("get", 3),
	              "fnv1a over static_string == over the same bytes");
	static_assert(xxh64::hash(s) == xxh64::hash("get", 3),
	              "xxh64 over static_string == over the same bytes");

	std::printf("hashes static_string OK: fnv1a/xxh64 accept a static_string\n");
}


// ---------------------------------------------------------------------------
// 3. mixer and jump_consistent_hash.
// ---------------------------------------------------------------------------

static void test_mixer_and_jump() {
	// mixer is an integer hash; just confirm it is non-trivial and deterministic.
	static_assert(mixer::mix(123456789ULL) == mixer::mix(123456789ULL), "mixer deterministic");
	static_assert(mixer::mix(0) != 0 || mixer::mix(1) != 1, "mixer is not the identity");

	// jump_consistent_hash lands in range and is stable.
	assert(jump_consistent_hash(std::uint64_t{42}, 10) < 10);
	assert(jump_consistent_hash(std::string_view("some/key"), 8) < 8);

	std::printf("hashes mixer/jump OK: deterministic mixer, in-range jump hash\n");
}


// ---------------------------------------------------------------------------
// 4. string-switch UDL dispatch (compile-time _fnv1a / _xx).
// ---------------------------------------------------------------------------

// A classic compile-time string switch: hash the runtime string with a 32-bit
// FNV-1a and compare it against literal hashes computed at compile time. Using
// fnv1ah32 on both sides keeps the switch value and the case labels the same
// width.
static int classify(std::string_view s) {
	switch (fnv1ah32::hash(s)) {
		case fnv1ah32::hash("get", 3):    return 1;
		case fnv1ah32::hash("put", 3):    return 2;
		case fnv1ah32::hash("delete", 6): return 3;
		default:                          return 0;
	}
}

static void test_string_switch() {
	assert(classify("get") == 1);
	assert(classify("put") == 2);
	assert(classify("delete") == 3);
	assert(classify("patch") == 0);

	// The _fnv1a UDL is a compile-time constant and must agree exactly with
	// fnv1ah32 of the same bytes, so _fnv1a case labels can be mixed with the
	// fnv1ah32-hashed switch value above. (Previously it hashed with fnv1ah64 and
	// narrowed to 32 bits, silently disagreeing with fnv1ah32 -- a footgun fixed.)
	static_assert("get"_fnv1a == fnv1ah32::hash("get"),
	              "_fnv1a UDL == fnv1ah32 of same bytes");
	static_assert("put"_fnv1a == fnv1ah32::hash("put"),
	              "_fnv1a UDL == fnv1ah32 of same bytes");
	static_assert("delete"_fnv1a == fnv1ah32::hash("delete"),
	              "_fnv1a UDL == fnv1ah32 of same bytes");
	static_assert("get"_fnv1a == fnv1ah32::hash("get", 3),
	              "_fnv1a UDL matches the const char*+len form too");
	constexpr std::uint32_t g = "get"_fnv1a;
	static_assert(g != 0, "_fnv1a UDL produced a constant");

	// _xx is the constexpr xxh64 UDL; it too is a usable compile-time constant.
	static_assert("hello"_xx == xxh64::hash("hello", 5), "_xx UDL == xxh64 of same bytes");
	constexpr std::uint64_t k = "hello"_xx;
	static_assert(k != 0, "xx UDL produced a constant");

	std::printf("hashes string-switch OK: get/put/delete dispatched, patch fell through\n");
}


int main() {
	test_hash_values();
	test_static_string_hash();
	test_mixer_and_jump();
	test_string_switch();
	std::printf("all hashes tests passed\n");
	return 0;
}
