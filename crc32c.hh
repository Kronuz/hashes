/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// CRC-32C (Castagnoli, reflected, init and final XOR 0xffffffff), as used by
// iSCSI, ext4, and kronuz::journal. Hardware instructions on x86 (SSE4.2) and
// ARMv8 (CRC32), chosen at runtime; a slice-by-8 table everywhere else and at
// compile time.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

#if defined(__x86_64__) || defined(_M_X64)
#include <nmmintrin.h>
#define HASHES_CRC32C_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#include <arm_acle.h>
#define HASHES_CRC32C_ARM 1
#if defined(__linux__) && !defined(__ARM_FEATURE_CRC32)
#include <sys/auxv.h>
#endif
#endif

namespace hashes {

namespace crc32c_detail {

inline constexpr std::uint32_t poly = 0x82f63b78u;  // reflected 0x1edc6f41

inline constexpr auto tables = [] {
	std::array<std::array<std::uint32_t, 256>, 8> t{};
	for (std::uint32_t i = 0; i < 256; ++i) {
		auto v = i;
		for (int bit = 0; bit < 8; ++bit) { v = (v >> 1) ^ ((v & 1) ? poly : 0); }
		t[0][i] = v;
	}
	for (std::size_t k = 1; k < 8; ++k) {
		for (std::size_t i = 0; i < 256; ++i) { t[k][i] = (t[k - 1][i] >> 8) ^ t[0][t[k - 1][i] & 0xff]; }
	}
	return t;
}();

// Advances a raw (non-inverted) CRC state over bytes, eight at a time.
constexpr std::uint32_t table_update(std::uint32_t crc, const unsigned char* p, std::size_t n) noexcept {
	for (; n >= 8; p += 8, n -= 8) {
		std::uint32_t lo = crc ^ (std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24);
		crc = tables[7][lo & 0xff] ^ tables[6][(lo >> 8) & 0xff] ^ tables[5][(lo >> 16) & 0xff] ^ tables[4][lo >> 24] ^
		      tables[3][p[4]] ^ tables[2][p[5]] ^ tables[1][p[6]] ^ tables[0][p[7]];
	}
	for (; n; ++p, --n) { crc = tables[0][(crc ^ *p) & 0xff] ^ (crc >> 8); }
	return crc;
}

// a * b modulo the CRC polynomial, in the reflected bit order (bit 31 is x^0).
constexpr std::uint32_t multiply(std::uint32_t a, std::uint32_t b) noexcept {
	std::uint32_t product = 0;
	for (std::uint32_t m = 1u << 31; m; m >>= 1) {
		if (a & m) { product ^= b; }
		b = (b & 1) ? (b >> 1) ^ poly : b >> 1;
	}
	return product;
}

// x^(8 * bytes) modulo the polynomial: the factor that shifts a CRC state past `bytes` zero bytes.
constexpr std::uint32_t shift_factor(std::uint64_t bytes) noexcept {
	std::uint32_t result = 1u << 31, square = 1u << 23;  // x^0 and x^8
	for (; bytes; bytes >>= 1) {
		if (bytes & 1) { result = multiply(result, square); }
		square = multiply(square, square);
	}
	return result;
}

// Three interleaved streams hide the 3-cycle latency of the CRC instruction.
inline constexpr std::size_t lane = 1024;
inline constexpr std::uint32_t shift_one_lane = shift_factor(lane);
inline constexpr std::uint32_t shift_two_lanes = shift_factor(2 * lane);

inline std::uint64_t load64(const unsigned char* p) noexcept {
	std::uint64_t v;
	std::memcpy(&v, p, 8);
	return v;
}

// Defines NAME(crc, p, n): three lanes per 3 KiB stride, merged with shift constants, then 8-byte and 1-byte tails.
#define HASHES_CRC32C_HARDWARE(NAME, TARGET, STEP8, STEP1) \
	__attribute__((target(TARGET))) inline std::uint32_t NAME(std::uint32_t crc, const unsigned char* p, std::size_t n) noexcept { \
		for (; n >= 3 * lane; p += 3 * lane, n -= 3 * lane) { \
			std::uint64_t a = crc, b = 0, c = 0; \
			for (std::size_t i = 0; i < lane; i += 8) { \
				a = STEP8(a, load64(p + i)); \
				b = STEP8(b, load64(p + lane + i)); \
				c = STEP8(c, load64(p + 2 * lane + i)); \
			} \
			crc = multiply(std::uint32_t(a), shift_two_lanes) ^ multiply(std::uint32_t(b), shift_one_lane) ^ std::uint32_t(c); \
		} \
		std::uint64_t v = crc; \
		for (; n >= 8; p += 8, n -= 8) { v = STEP8(v, load64(p)); } \
		crc = std::uint32_t(v); \
		for (; n; ++p, --n) { crc = STEP1(crc, *p); } \
		return crc; \
	}

#if HASHES_CRC32C_X86
HASHES_CRC32C_HARDWARE(x86_update, "sse4.2", _mm_crc32_u64, _mm_crc32_u8)
inline bool hardware_available() noexcept {
	static const bool available = __builtin_cpu_supports("sse4.2");
	return available;
}
#elif HASHES_CRC32C_ARM
#define HASHES_CRC32C_ARM_STEP8(c, v) std::uint64_t(__crc32cd(std::uint32_t(c), (v)))
HASHES_CRC32C_HARDWARE(arm_update, "+crc", HASHES_CRC32C_ARM_STEP8, __crc32cb)
#undef HASHES_CRC32C_ARM_STEP8
inline bool hardware_available() noexcept {
#if defined(__ARM_FEATURE_CRC32) || defined(__APPLE__)
	return true;
#elif defined(__linux__)
	static const bool available = (getauxval(AT_HWCAP) & HWCAP_CRC32) != 0;
	return available;
#else
	return false;
#endif
}
#else
inline bool hardware_available() noexcept { return false; }
#endif
#undef HASHES_CRC32C_HARDWARE

inline std::uint32_t update(std::uint32_t crc, const unsigned char* p, std::size_t n) noexcept {
#if HASHES_CRC32C_X86
	if (hardware_available()) { return x86_update(crc, p, n); }
#elif HASHES_CRC32C_ARM
	if (hardware_available()) { return arm_update(crc, p, n); }
#endif
	return table_update(crc, p, n);
}

}  // namespace crc32c_detail

// Incremental CRC-32C: feed bytes in any number of pieces, then read value().
class Crc32c {
public:
	constexpr Crc32c() noexcept = default;
	constexpr explicit Crc32c(std::uint32_t value) noexcept : state_(~value) {}

	Crc32c& update(const void* data, std::size_t size) noexcept {
		state_ = crc32c_detail::update(state_, static_cast<const unsigned char*>(data), size);
		return *this;
	}
	constexpr Crc32c& update(std::string_view bytes) noexcept {
		if (std::is_constant_evaluated()) {
			for (char ch : bytes) { state_ = crc32c_detail::tables[0][(state_ ^ static_cast<unsigned char>(ch)) & 0xff] ^ (state_ >> 8); }
			return *this;
		}
		return update(bytes.data(), bytes.size());
	}
	constexpr std::uint32_t value() const noexcept { return ~state_; }

private:
	std::uint32_t state_ = ~0u;
};

constexpr std::uint32_t crc32c(std::string_view bytes) noexcept { return Crc32c{}.update(bytes).value(); }

inline std::uint32_t crc32c(const void* data, std::size_t size) noexcept { return Crc32c{}.update(data, size).value(); }

// CRC of A‖B from crc32c(A), crc32c(B), and B's length, without rereading the bytes.
constexpr std::uint32_t crc32c_combine(std::uint32_t crc_a, std::uint32_t crc_b, std::uint64_t size_b) noexcept {
	// Raw states: ~crc. State after A‖B = shift(stateA, |B|) ^ raw_crc(B from 0); raw_crc(B from 0) = stateB ^ shift(~0, |B|).
	auto shift = crc32c_detail::shift_factor(size_b);
	return ~(crc32c_detail::multiply(~crc_a, shift) ^ ~crc_b ^ crc32c_detail::multiply(~0u, shift));
}

}  // namespace hashes
