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
    {0x05000000U, 0x057FFFFFU, 8U, 2U}, // A-Bus dummy
    {0x05800000U, 0x058FFFFFU, 40U, 40U}, // CD Block CS2
    {0x05A00000U, 0x05BFFFFFU, 40U, 2U}, // SCSP
    {0x05C00000U, 0x05C7FFFFU, 22U, 2U}, // VDP1 VRAM
    {0x05C80000U, 0x05CFFFFFU, 22U, 2U}, // VDP1 FB
    {0x05D00000U, 0x05D7FFFFU, 14U, 2U}, // VDP1 regs
    {0x05E00000U, 0x05FBFFFFU, 20U, 2U}, // VDP2
    {0x05FE0000U, 0x05FEFFFFU, 4U, 2U}, // SCU regs
    {0x06000000U, 0x07FFFFFFU, 2U, 2U}, // High WRAM
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
