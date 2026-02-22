#include "busarb/ymir_timing.hpp"

#include <cstdint>

namespace busarb {
namespace {

struct RegionTiming {
  std::uint32_t start;
  std::uint32_t end;
  std::uint32_t read_cycles;
  std::uint32_t write_cycles;
};

constexpr RegionTiming kRegionTimings[] = {
    {0x00000000U, 0x00FFFFFFU, 2U, 2U}, // BIOS ROM
    {0x01000000U, 0x017FFFFFU, 4U, 2U}, // SMPC
    {0x01800000U, 0x01FFFFFFU, 2U, 2U}, // Backup RAM
    {0x02000000U, 0x02FFFFFFU, 2U, 2U}, // Low WRAM
    {0x10000000U, 0x1FFFFFFFU, 4U, 2U}, // MINIT/SINIT
    {0x20000000U, 0x4FFFFFFFU, 2U, 2U}, // A-Bus CS0/CS1
    {0x50000000U, 0x57FFFFFFU, 8U, 2U}, // A-Bus dummy
    {0x58000000U, 0x58FFFFFFU, 40U, 40U}, // CD Block CS2
    {0x5A000000U, 0x5BFFFFFFU, 40U, 2U}, // SCSP
    {0x5C000000U, 0x5C7FFFFFU, 22U, 2U}, // VDP1 VRAM
    {0x5C800000U, 0x5CFFFFFFU, 22U, 2U}, // VDP1 FB
    {0x5D000000U, 0x5D7FFFFFU, 14U, 2U}, // VDP1 regs
    {0x5E000000U, 0x5FBFFFFFU, 20U, 2U}, // VDP2
    {0x5FE00000U, 0x5FEFFFFFU, 4U, 2U}, // SCU regs
    {0x60000000U, 0x7FFFFFFFU, 2U, 2U}, // High WRAM
};

} // namespace

std::uint32_t ymir_access_cycles(void *, std::uint32_t addr, bool is_write, std::uint8_t) {
  for (const auto &region : kRegionTimings) {
    if (addr >= region.start && addr <= region.end) {
      return is_write ? region.write_cycles : region.read_cycles;
    }
  }
  return is_write ? 2U : 4U;
}

} // namespace busarb
