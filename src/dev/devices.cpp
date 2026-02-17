#include "dev/devices.hpp"

namespace saturnis::dev {
namespace {

constexpr std::uint32_t kDisplayStatusAddr = 0x05F00010U;

[[nodiscard]] std::uint32_t lane_shift(std::uint32_t addr, std::uint8_t size) {
  if (size == 1U) {
    return (addr & 0x3U) * 8U;
  }
  if (size == 2U) {
    return (addr & 0x2U) * 8U;
  }
  return 0U;
}

[[nodiscard]] std::uint32_t size_mask(std::uint8_t size) {
  if (size == 1U) {
    return 0xFFU;
  }
  if (size == 2U) {
    return 0xFFFFU;
  }
  return 0xFFFFFFFFU;
}

} // namespace

std::uint32_t DeviceHub::read(std::uint64_t, int, std::uint32_t addr, std::uint8_t size) {
  // TODO: Expand to explicit per-device register models (SMPC/SCU/VDP1/VDP2/SCSP).
  const std::uint32_t word_addr = addr & ~0x3U;
  const auto it = mmio_regs_.find(word_addr);
  std::uint32_t value = 0U;

  if (it != mmio_regs_.end()) {
    value = it->second;
  } else if (word_addr == kDisplayStatusAddr) {
    value = 0x1U; // deterministic display-ready default bit.
  }

  const std::uint32_t shift = lane_shift(addr, size);
  return (value >> shift) & size_mask(size);
}

void DeviceHub::write(std::uint64_t t, int cpu, std::uint32_t addr, std::uint8_t size, std::uint32_t value) {
  writes_.push_back(MmioWriteLog{t, cpu, addr, value});

  const std::uint32_t word_addr = addr & ~0x3U;
  const std::uint32_t shift = lane_shift(addr, size);
  const std::uint32_t mask = size_mask(size) << shift;
  const std::uint32_t old_value = mmio_regs_[word_addr];
  mmio_regs_[word_addr] = (old_value & ~mask) | ((value << shift) & mask);
}

const std::vector<MmioWriteLog> &DeviceHub::writes() const { return writes_; }

} // namespace saturnis::dev
