// CRC-32C: known vectors, hardware against the table path, incremental and combine.
#include "crc32c.hh"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); ++failures; } \
	} while (0)

static std::uint32_t reference(const unsigned char* p, std::size_t n) {
	return ~hashes::crc32c_detail::table_update(~0u, p, n);
}

static std::uint32_t bitwise(const unsigned char* p, std::size_t n) {
	std::uint32_t crc = ~0u;
	for (std::size_t i = 0; i < n; ++i) {
		crc ^= p[i];
		for (int bit = 0; bit < 8; ++bit) { crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78u : 0); }
	}
	return ~crc;
}

int main() {
	// RFC 3720 (iSCSI) and the common check value.
	static_assert(hashes::crc32c("123456789") == 0xe3069283u);
	static_assert(hashes::crc32c("") == 0u);
	CHECK(hashes::crc32c(std::string_view("123456789")) == 0xe3069283u);
	std::vector<unsigned char> zeros(32, 0), ones(32, 0xff), inc(32), dec(32);
	for (int i = 0; i < 32; ++i) { inc[i] = static_cast<unsigned char>(i); dec[i] = static_cast<unsigned char>(31 - i); }
	CHECK(hashes::crc32c(zeros.data(), 32) == 0x8a9136aau);
	CHECK(hashes::crc32c(ones.data(), 32) == 0x62a8ab43u);
	CHECK(hashes::crc32c(inc.data(), 32) == 0x46dd794eu);
	CHECK(hashes::crc32c(dec.data(), 32) == 0x113fdb5cu);

	std::mt19937_64 rng(20261011);
	std::vector<unsigned char> buffer(20000 + 16);
	for (auto& b : buffer) { b = static_cast<unsigned char>(rng()); }

	// Every length across the 3-lane threshold, at every alignment.
	for (std::size_t offset = 0; offset < 8; ++offset) {
		for (std::size_t n = 0; n <= 3 * 1024 * 2 + 64; n += (n < 256 ? 1 : 7)) {
			auto expected = reference(buffer.data() + offset, n);
			CHECK(hashes::crc32c(buffer.data() + offset, n) == expected);
		}
	}
	CHECK(reference(buffer.data(), 1000) == bitwise(buffer.data(), 1000));
	CHECK(hashes::crc32c(buffer.data(), 20000) == bitwise(buffer.data(), 20000));

	// Incremental updates in random pieces equal the one-shot value.
	for (int round = 0; round < 200; ++round) {
		std::size_t n = rng() % buffer.size();
		hashes::Crc32c crc;
		for (std::size_t at = 0; at < n;) {
			std::size_t piece = std::min<std::size_t>(n - at, rng() % 4000);
			crc.update(buffer.data() + at, piece);
			at += piece;
		}
		CHECK(crc.value() == hashes::crc32c(buffer.data(), n));
	}

	// Empty input: the CRC of nothing is 0, and empty updates change nothing, whatever the state.
	static_assert(hashes::Crc32c{}.update(std::string_view{}).value() == 0u);
	CHECK(hashes::crc32c(nullptr, 0) == 0u);
	CHECK(hashes::Crc32c{}.update(nullptr, 0).value() == 0u);
	CHECK(hashes::Crc32c{}.update(std::string_view{}).value() == 0u);
	for (int round = 0; round < 50; ++round) {
		auto seed = static_cast<std::uint32_t>(rng());
		CHECK(hashes::Crc32c(seed).update(std::string_view{}).update(nullptr, 0).value() == seed);
		CHECK(hashes::Crc32c(seed).update(buffer.data(), 0).value() == seed);
	}

	// Every two-way split of lengths around the lane and stride boundaries, including empty halves.
	for (std::size_t n : {0, 1, 7, 8, 9, 63, 1023, 1024, 1025, 2047, 3071, 3072, 3073, 3100}) {
		auto expected = hashes::crc32c(buffer.data(), n);
		for (std::size_t cut = 0; cut <= n; ++cut) {
			hashes::Crc32c crc;
			crc.update(buffer.data(), cut).update(buffer.data() + cut, n - cut);
			CHECK(crc.value() == expected);
		}
	}

	// Pieces of random size (often empty), through both overloads, and one byte at a time.
	for (int round = 0; round < 300; ++round) {
		std::size_t n = rng() % buffer.size();
		auto expected = reference(buffer.data(), n);
		hashes::Crc32c by_pointer, by_view;
		for (std::size_t at = 0; at < n;) {
			std::size_t piece = std::min<std::size_t>(n - at, rng() % (round % 3 ? 5000 : 3));
			by_pointer.update(buffer.data() + at, piece);
			by_view.update(std::string_view(reinterpret_cast<const char*>(buffer.data()) + at, piece));
			at += piece;
		}
		CHECK(by_pointer.value() == expected);
		CHECK(by_view.value() == expected);
	}
	{
		hashes::Crc32c crc;
		for (std::size_t i = 0; i < 5000; ++i) { crc.update(buffer.data() + i, 1); }
		CHECK(crc.value() == reference(buffer.data(), 5000));
	}

	// The hardware routine against the table routine from arbitrary states, lengths and alignments.
	if (hashes::crc32c_detail::hardware_available()) {
		for (int round = 0; round < 2000; ++round) {
			auto state = static_cast<std::uint32_t>(rng());
			std::size_t offset = rng() % 16, n = rng() % (round % 10 ? 1500 : 12000);
			auto expected = hashes::crc32c_detail::table_update(state, buffer.data() + offset, n);
#if HASHES_CRC32C_X86
			auto actual = hashes::crc32c_detail::x86_update(state, buffer.data() + offset, n);
#else
			auto actual = hashes::crc32c_detail::arm_update(state, buffer.data() + offset, n);
#endif
			CHECK(actual == expected);
			CHECK(hashes::crc32c_detail::update(state, buffer.data() + offset, n) == expected);
		}
	}

	// Combine and resume.
	for (int round = 0; round < 200; ++round) {
		std::size_t a = rng() % 10000, b = rng() % 10000;
		auto ca = hashes::crc32c(buffer.data(), a);
		auto cb = hashes::crc32c(buffer.data() + a, b);
		CHECK(hashes::crc32c_combine(ca, cb, b) == hashes::crc32c(buffer.data(), a + b));
		CHECK(hashes::Crc32c(ca).update(buffer.data() + a, b).value() == hashes::crc32c(buffer.data(), a + b));
	}

	std::printf("crc32c: hardware %s, %s\n", hashes::crc32c_detail::hardware_available() ? "yes" : "no",
		failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
