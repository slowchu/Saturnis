#include "dev/devices.hpp"

namespace saturnis::dev {
namespace {

constexpr std::uint32_t kDisplayStatusAddr = 0x05F00010U;
constexpr std::uint32_t kScuImsAddr = 0x05FE00A0U;
constexpr std::uint32_t kScuImsWritableMask = 0x0000FFFFU;

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

  std::uint32_t value = 0U;
  if (word_addr == kDisplayStatusAddr) {
    // Deterministic display-ready status bit (modeled as read-only for now).
    value = 0x1U;
  } else if (word_addr == kScuImsAddr) {
    value = mmio_regs_[kScuImsAddr] & kScuImsWritableMask;
  } else {
    const auto it = mmio_regs_.find(word_addr);
    if (it != mmio_regs_.end()) {
      value = it->second;
    }
  }

  const std::uint32_t shift = lane_shift(addr, size);
  return (value >> shift) & size_mask(size);
}

void DeviceHub::write(std::uint64_t t, int cpu, std::uint32_t addr, std::uint8_t size, std::uint32_t value) {
  writes_.push_back(MmioWriteLog{t, cpu, addr, value});

  const std::uint32_t word_addr = addr & ~0x3U;
  const std::uint32_t shift = lane_shift(addr, size);
  const std::uint32_t write_mask = size_mask(size) << shift;

  // Display status is read-only in this simplified model.
  if (word_addr == kDisplayStatusAddr) {
    return;
  }

  const std::uint32_t old_value = mmio_regs_[word_addr];
  const std::uint32_t merged = (old_value & ~write_mask) | ((value << shift) & write_mask);
  if (word_addr == kScuImsAddr) {
    mmio_regs_[word_addr] = merged & kScuImsWritableMask;
    return;
  }
  mmio_regs_[word_addr] = merged;
}

const std::vector<MmioWriteLog> &DeviceHub::writes() const { return writes_; }

} // namespace saturnis::dev
