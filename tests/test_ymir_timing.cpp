#include "busarb/ymir_timing.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

struct Row {
  std::uint32_t start;
  std::uint32_t end;
  std::uint32_t read_cycles;
  std::uint32_t write_cycles;
};

void check(bool cond, const char *msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    std::exit(1);
  }
}

} // namespace

int main() {
  const std::array<Row, 15> rows = {{
      {0x00000000U, 0x00FFFFFFU, 2U, 2U},
      {0x01000000U, 0x017FFFFFU, 4U, 2U},
      {0x01800000U, 0x01FFFFFFU, 2U, 2U},
      {0x02000000U, 0x02FFFFFFU, 2U, 2U},
      {0x10000000U, 0x1FFFFFFFU, 4U, 2U},
      {0x20000000U, 0x4FFFFFFFU, 2U, 2U},
      {0x05000000U, 0x057FFFFFU, 8U, 2U},
      {0x05800000U, 0x058FFFFFU, 40U, 40U},
      {0x05A00000U, 0x05BFFFFFU, 40U, 2U},
      {0x05C00000U, 0x05C7FFFFU, 22U, 2U},
      {0x05C80000U, 0x05CFFFFFU, 22U, 2U},
      {0x05D00000U, 0x05D7FFFFU, 14U, 2U},
      {0x05E00000U, 0x05FBFFFFU, 20U, 2U},
      {0x05FE0000U, 0x05FEFFFFU, 4U, 2U},
      {0x06000000U, 0x07FFFFFFU, 2U, 2U},
  }};

  for (const auto &row : rows) {
    check(busarb::ymir_access_cycles(nullptr, row.start, false, 4U) == row.read_cycles, "read start mismatch");
    check(busarb::ymir_access_cycles(nullptr, row.start, true, 4U) == row.write_cycles, "write start mismatch");
    check(busarb::ymir_access_cycles(nullptr, row.end, false, 4U) == row.read_cycles, "read end mismatch");
    check(busarb::ymir_access_cycles(nullptr, row.end, true, 4U) == row.write_cycles, "write end mismatch");
  }

  check(busarb::ymir_access_cycles(nullptr, 0xFFFFFFFFU, false, 4U) == 4U, "unmapped read fallback mismatch");
  check(busarb::ymir_access_cycles(nullptr, 0xFFFFFFFFU, true, 4U) == 2U, "unmapped write fallback mismatch");

  std::cout << "ymir timing tests passed\n";
  return 0;
}
