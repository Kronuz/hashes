// CRC-32C throughput: hardware path versus the slice-by-8 table and the byte-at-a-time table.
#include "crc32c.hh"

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

template <typename F>
static double gb_per_s(F&& f, std::size_t bytes) {
	using clock = std::chrono::steady_clock;
	volatile std::uint32_t sink = 0;
	double best = 0;
	for (int trial = 0; trial < 7; ++trial) {
		std::size_t total = 0;
		auto start = clock::now();
		while (std::chrono::duration<double>(clock::now() - start).count() < 0.2) {
			for (int i = 0; i < 256; ++i) { sink = sink + f(); }
			total += 256 * bytes;
		}
		double seconds = std::chrono::duration<double>(clock::now() - start).count();
		best = std::max(best, total / seconds / 1e9);
	}
	return best;
}

int main() {
	std::mt19937_64 rng(1);
	std::vector<unsigned char> data(1 << 20);
	for (auto& b : data) { b = static_cast<unsigned char>(rng()); }
	std::printf("%-10s %12s %12s %12s\n", "bytes", "hw GB/s", "slice8 GB/s", "byte GB/s");
	for (std::size_t n : {64u, 512u, 4096u, 65536u, 1u << 20}) {
		auto hw = gb_per_s([&] { return hashes::crc32c(data.data(), n); }, n);
		auto s8 = gb_per_s([&] { return ~hashes::crc32c_detail::table_update(~0u, data.data(), n); }, n);
		auto b1 = gb_per_s([&] {
			std::uint32_t crc = ~0u;
			for (std::size_t i = 0; i < n; ++i) { crc = hashes::crc32c_detail::tables[0][(crc ^ data[i]) & 0xff] ^ (crc >> 8); }
			return ~crc;
		}, n);
		std::printf("%-10zu %12.2f %12.2f %12.2f\n", n, hw, s8, b1);
	}
}
