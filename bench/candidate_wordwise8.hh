// Candidate, not library code.
//
// A wide-lane 64-bit string hash proposed beside `wordwise`. `wordwise` mixes one eight-byte
// word per dependent multiply, so its time grows with the key: at 64 bytes XXH3 is 1.3x
// faster, at 128 KiB 9x. This candidate keeps `wordwise`'s multiplier and step and changes
// the shape around it: up to 64 bytes it reads the head and the tail with overlapping loads
// (no loop, no byte assembly), and longer keys run eight independent lanes over 64-byte
// blocks that a core executes in parallel.
//
// Its values differ from `wordwise`'s. Call sites store `wordwise` values, so a hash like
// this would ship as a new family, never as a change to `wordwise`. bench/compare.cc times it,
// requires with --check that every input byte reaches the hash at every length to 600, and
// prints its avalanche and duplicate counts with --quality. It is written from the
// algorithm; nothing in it comes from xxHash or absl.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace candidate {

namespace detail {

constexpr std::uint64_t P = 0x9e3779b97f4a7c15ULL;

inline std::uint64_t load64(const char* p) {
	std::uint64_t w;
	std::memcpy(&w, p, 8);
	return w;
}

inline std::uint64_t load32(const char* p) {
	std::uint32_t w;
	std::memcpy(&w, p, 4);
	return w;
}

// The `wordwise` step: xor in a word, multiply, fold the high bits down.
inline void step(std::uint64_t& h, std::uint64_t w) {
	h = (h ^ w) * P;
	h ^= h >> 29;
}

inline std::uint64_t finish(std::uint64_t h) {
	h = (h ^ (h >> 32)) * P;
	h ^= h >> 29;
	h *= P;
	return h ^ (h >> 32);
}

}  // namespace detail

// Little-endian machines only: the loads are native.
inline std::uint64_t wordwise8(const char* p, std::size_t len) {
	using namespace detail;
	const std::uint64_t seed = 0x100000001b3ULL ^ (static_cast<std::uint64_t>(len) * P);
	if (len <= 16) {
		std::uint64_t lo, hi;
		if (len >= 8) {
			lo = load64(p);
			hi = load64(p + len - 8);
		} else if (len >= 4) {
			lo = load32(p);
			hi = load32(p + len - 4);
		} else if (len > 0) {
			lo = (static_cast<std::uint64_t>(static_cast<unsigned char>(p[0])) << 16) |
			     (static_cast<std::uint64_t>(static_cast<unsigned char>(p[len >> 1])) << 8) |
			     static_cast<unsigned char>(p[len - 1]);
			hi = 0;
		} else {
			lo = hi = 0;
		}
		std::uint64_t h = seed;
		step(h, lo);
		step(h, hi ^ 0xff51afd7ed558ccdULL);
		return finish(h);
	}
	if (len <= 32) {  // 17..32 bytes: two loads from the head, two from the tail, overlapping
		std::uint64_t h = seed;
		step(h, load64(p));
		step(h, load64(p + 8));
		step(h, load64(p + len - 16));
		step(h, load64(p + len - 8));
		return finish(h);
	}
	if (len <= 64) {  // 33..64 bytes: four loads from the head, four from the tail
		std::uint64_t h = seed;
		for (int k = 0; k < 4; ++k) step(h, load64(p + 8 * k));
		for (int k = 0; k < 4; ++k) step(h, load64(p + len - 32 + 8 * k));
		return finish(h);
	}
	std::uint64_t lane[8];
	for (int k = 0; k < 8; ++k) lane[k] = seed + P * static_cast<std::uint64_t>(k);
	std::size_t i = 0;
	for (; i + 64 <= len; i += 64) {
		for (int k = 0; k < 8; ++k) step(lane[k], load64(p + i + 8 * k));
	}
	if (i < len) {  // the last 64 bytes, overlapping the previous block (len > 64 here)
		i = len - 64;
		for (int k = 0; k < 8; ++k) step(lane[k], load64(p + i + 8 * k));
	}
	std::uint64_t h = seed;
	for (int k = 0; k < 8; ++k) {
		h ^= lane[k] * static_cast<std::uint64_t>(2 * k + 1);
		h = ((h << 7) | (h >> 57)) * P;
	}
	return finish(h);
}

}  // namespace candidate
