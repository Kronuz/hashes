// CRC-32C: known vectors, hardware against the table path, incremental and combine.
#include "crc32c.hh"

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
