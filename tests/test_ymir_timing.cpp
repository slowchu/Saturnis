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
      {0x50000000U, 0x57FFFFFFU, 8U, 2U},
      {0x58000000U, 0x58FFFFFFU, 40U, 40U},
      {0x5A000000U, 0x5BFFFFFFU, 40U, 2U},
      {0x5C000000U, 0x5C7FFFFFU, 22U, 2U},
      {0x5C800000U, 0x5CFFFFFFU, 22U, 2U},
      {0x5D000000U, 0x5D7FFFFFU, 14U, 2U},
      {0x5E000000U, 0x5FBFFFFFU, 20U, 2U},
      {0x5FE00000U, 0x5FEFFFFFU, 4U, 2U},
      {0x60000000U, 0x7FFFFFFFU, 2U, 2U},
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
