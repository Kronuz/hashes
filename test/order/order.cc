#include "crc32c.hh"
#include "hashes.hh"
#include "static_string.hh"

int main() {
	bool crc = hashes::crc32c(std::string_view("123456789")) == 0xe3069283u;
	bool fnv = fnv1ah32::hash("a", 1) == 0xe40c292cUL;
	return crc && fnv ? 0 : 1;
}
