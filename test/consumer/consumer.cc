#include "crc32c.hh"

int main() { return hashes::crc32c(std::string_view("123456789")) == 0xe3069283u ? 0 : 1; }
