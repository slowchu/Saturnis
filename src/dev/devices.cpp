#include "dev/devices.hpp"

namespace saturnis::dev {

std::uint32_t DeviceHub::read(std::uint64_t, int, std::uint32_t addr, std::uint8_t) {
  // TODO: Expand per-device semantics (SMPC/SCU/VDP1/VDP2/SCSP).
  if (addr == 0x05F00010U) {
    return 0x1U; // pretend display-ready status bit.
  }
  return 0U;
}

void DeviceHub::write(std::uint64_t t, int cpu, std::uint32_t addr, std::uint8_t, std::uint32_t value) {
  writes_.push_back(MmioWriteLog{t, cpu, addr, value});
}

const std::vector<MmioWriteLog> &DeviceHub::writes() const { return writes_; }

} // namespace saturnis::dev
