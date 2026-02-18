#include "dev/devices.hpp"

#include <optional>

namespace saturnis::dev {
namespace {

constexpr std::uint32_t kDisplayStatusAddr = 0x05F00010U;
constexpr std::uint32_t kScuImsAddr = 0x05FE00A0U;
constexpr std::uint32_t kScuIstAddr = 0x05FE00A4U;
constexpr std::uint32_t kScuIstClearAddr = 0x05FE00A8U;
constexpr std::uint32_t kScuIstSourceSetAddr = 0x05FE00ACU;
constexpr std::uint32_t kScuIstSourceClearAddr = 0x05FE00B0U;
constexpr std::uint32_t kScuDma0SrcAddr = 0x05FE0020U;
constexpr std::uint32_t kScuDma0SizeAddr = 0x05FE0028U;
constexpr std::uint32_t kScuDma0CtrlAddr = 0x05FE002CU;
constexpr std::uint32_t kSmpcStatusAddr = 0x05D00080U;
constexpr std::uint32_t kSmpcCommandAddr = 0x05D00084U;
constexpr std::uint32_t kSmpcCommandResultAddr = 0x05D00088U;
constexpr std::uint32_t kVdp2TvmdAddr = 0x05F80000U;
constexpr std::uint32_t kVdp2TvstatAddr = 0x05F80004U;
constexpr std::uint32_t kScspMcierAddr = 0x05C00000U;
constexpr std::uint32_t kVdp1ScuIrqBridgeAddr = 0x05D0008CU;
constexpr std::uint32_t kVdp1EventTriggerAddr = 0x05D00090U;
constexpr std::uint32_t kVdp1EventStatusAddr = 0x05D00094U;
constexpr std::uint32_t kVdp1ScuIrqMask = 0x00000020U;

struct MmioRegisterSpec {
  std::uint32_t reset_value = 0U;
  std::uint32_t writable_mask = 0xFFFFFFFFU;
};

[[nodiscard]] std::optional<MmioRegisterSpec> register_spec(std::uint32_t word_addr) {
  if (word_addr == kDisplayStatusAddr) {
    return MmioRegisterSpec{0x1U, 0x00000000U};
  }
  if (word_addr == kScuImsAddr) {
    return MmioRegisterSpec{0x0U, 0x0000FFFFU};
  }
  if (word_addr == kScuIstAddr) {
    return MmioRegisterSpec{0x0U, 0x00000000U};
  }
  if (word_addr == kScuIstClearAddr) {
    return MmioRegisterSpec{0x0U, 0x0000FFFFU};
  }
  if (word_addr == kScuIstSourceSetAddr) {
    return MmioRegisterSpec{0x0U, 0x0000FFFFU};
  }
  if (word_addr == kScuIstSourceClearAddr) {
    return MmioRegisterSpec{0x0U, 0x0000FFFFU};
  }
  if (word_addr == kScuDma0SrcAddr) {
    return MmioRegisterSpec{0x00000000U, 0xFFFFFFFFU};
  }
  if (word_addr == kScuDma0SizeAddr) {
    return MmioRegisterSpec{0x00000000U, 0x000FFFFFU};
  }
  if (word_addr == kScuDma0CtrlAddr) {
    return MmioRegisterSpec{0x00000000U, 0x00000017U};
  }
  if (word_addr == kSmpcStatusAddr) {
    return MmioRegisterSpec{0x1U, 0x00000000U};
  }
  if (word_addr == kSmpcCommandAddr) {
    return MmioRegisterSpec{0x0U, 0x000000FFU};
  }
  if (word_addr == kSmpcCommandResultAddr) {
    return MmioRegisterSpec{0x0U, 0x00000000U};
  }
  if (word_addr == kVdp2TvmdAddr) {
    return MmioRegisterSpec{0x00000000U, 0x0000FFFFU};
  }
  if (word_addr == kVdp2TvstatAddr) {
    return MmioRegisterSpec{0x00000008U, 0x00000000U};
  }
  if (word_addr == kScspMcierAddr) {
    return MmioRegisterSpec{0x00000000U, 0x000007FFU};
  }
  if (word_addr == kVdp1ScuIrqBridgeAddr) {
    return MmioRegisterSpec{0x00000000U, 0x00000001U};
  }
  if (word_addr == kVdp1EventTriggerAddr) {
    return MmioRegisterSpec{0x00000000U, 0x00000001U};
  }
  if (word_addr == kVdp1EventStatusAddr) {
    return MmioRegisterSpec{0x00000000U, 0x00000000U};
  }
  return std::nullopt;
}

[[nodiscard]] std::uint32_t lane_shift(std::uint32_t addr, std::uint8_t size) {
  if (size == 1U) {
    return (3U - (addr & 0x3U)) * 8U;
  }
  if (size == 2U) {
    return (2U - (addr & 0x2U)) * 8U;
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

[[nodiscard]] std::uint32_t materialize_register_value(const std::optional<MmioRegisterSpec> &spec,
                                                       std::uint32_t persisted_value) {
  if (!spec.has_value()) {
    return persisted_value;
  }

  const auto reg = *spec;
  return (reg.reset_value & ~reg.writable_mask) | (persisted_value & reg.writable_mask);
}

[[nodiscard]] std::uint32_t read_persisted_or_zero(const std::unordered_map<std::uint32_t, std::uint32_t> &regs,
                                                   std::uint32_t word_addr) {
  const auto it = regs.find(word_addr);
  return (it != regs.end()) ? it->second : 0U;
}

} // namespace

std::uint32_t DeviceHub::read(std::uint64_t, int, std::uint32_t addr, std::uint8_t size) {
  const std::uint32_t word_addr = addr & ~0x3U;

  std::uint32_t value = 0U;
  if (word_addr == kScuIstAddr) {
    const auto ims_spec = register_spec(kScuImsAddr);
    const std::uint32_t ims = materialize_register_value(ims_spec, read_persisted_or_zero(mmio_regs_, kScuImsAddr));
    value = (scu_interrupt_pending_ | scu_interrupt_source_pending_) & ~ims;
  } else if (word_addr == kScuIstSourceSetAddr) {
    value = scu_interrupt_source_pending_ & 0x0000FFFFU;
  } else if (word_addr == kSmpcCommandAddr) {
    value = smpc_last_command_ & 0xFFU;
  } else if (word_addr == kSmpcCommandResultAddr) {
    value = smpc_command_result_;
  } else if (word_addr == kVdp1ScuIrqBridgeAddr) {
    value = vdp1_irq_level_ & 0x1U;
  } else if (word_addr == kVdp1EventStatusAddr) {
    value = (vdp1_event_counter_ & 0xFFU) | ((vdp1_irq_level_ & 0x1U) << 8U);
  } else {
    const auto spec = register_spec(word_addr);
    const std::uint32_t persisted_value = read_persisted_or_zero(mmio_regs_, word_addr);
    value = materialize_register_value(spec, persisted_value);
  }

  const std::uint32_t shift = lane_shift(addr, size);
  return (value >> shift) & size_mask(size);
}

void DeviceHub::write(std::uint64_t t, int cpu, std::uint32_t addr, std::uint8_t size, std::uint32_t value) {
  writes_.push_back(MmioWriteLog{t, cpu, addr, value});

  const std::uint32_t word_addr = addr & ~0x3U;
  const auto spec = register_spec(word_addr);

  const std::uint32_t shift = lane_shift(addr, size);
  const std::uint32_t lane_mask = size_mask(size) << shift;
  const std::uint32_t write_bits = (value << shift) & lane_mask;

  if (word_addr == kScuIstAddr) {
    const std::uint32_t masked_bits = write_bits & 0x0000FFFFU;
    scu_interrupt_pending_ |= masked_bits;
    return;
  }

  if (word_addr == kScuIstClearAddr) {
    const std::uint32_t masked_bits = write_bits & 0x0000FFFFU;
    scu_interrupt_pending_ &= ~masked_bits;
    scu_interrupt_source_pending_ &= ~masked_bits;
    return;
  }

  if (word_addr == kScuIstSourceSetAddr) {
    const std::uint32_t masked_bits = write_bits & 0x0000FFFFU;
    scu_interrupt_source_pending_ |= masked_bits;
    return;
  }

  if (word_addr == kScuIstSourceClearAddr) {
    const std::uint32_t masked_bits = write_bits & 0x0000FFFFU;
    scu_interrupt_source_pending_ &= ~masked_bits;
    return;
  }


  if (word_addr == kSmpcCommandAddr) {
    const std::uint32_t command_byte = write_bits & 0x000000FFU;
    smpc_last_command_ = command_byte;
    smpc_command_result_ = 0xA5000000U | command_byte;
    return;
  }

  if (word_addr == kVdp1ScuIrqBridgeAddr) {
    vdp1_irq_level_ = write_bits & 0x1U;
    if ((vdp1_irq_level_ & 0x1U) != 0U) {
      scu_interrupt_source_pending_ |= kVdp1ScuIrqMask;
    } else {
      scu_interrupt_source_pending_ &= ~kVdp1ScuIrqMask;
    }
    return;
  }

  if (word_addr == kVdp1EventTriggerAddr) {
    if ((write_bits & 0x1U) != 0U) {
      ++vdp1_event_counter_;
      vdp1_irq_level_ = 1U;
      scu_interrupt_source_pending_ |= kVdp1ScuIrqMask;
    }
    return;
  }

  const std::uint32_t persisted_value = read_persisted_or_zero(mmio_regs_, word_addr);
  const std::uint32_t writable_mask = spec.has_value() ? spec->writable_mask : 0xFFFFFFFFU;
  const std::uint32_t masked_write = lane_mask & writable_mask;
  if (masked_write == 0U) {
    return;
  }

  const std::uint32_t next_value = (persisted_value & ~masked_write) | (write_bits & masked_write);
  mmio_regs_[word_addr] = next_value;
}

const std::vector<MmioWriteLog> &DeviceHub::writes() const { return writes_; }

} // namespace saturnis::dev
