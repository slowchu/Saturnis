#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace saturnis::dev {

struct MmioWriteLog {
  std::uint64_t t = 0;
  int cpu = 0;
  std::uint32_t addr = 0;
  std::uint32_t value = 0;
};

class DeviceHub {
public:
  [[nodiscard]] std::uint32_t read(std::uint64_t t, int cpu, std::uint32_t addr, std::uint8_t size);
  void write(std::uint64_t t, int cpu, std::uint32_t addr, std::uint8_t size, std::uint32_t value);
  [[nodiscard]] const std::vector<MmioWriteLog> &writes() const;

private:
  std::vector<MmioWriteLog> writes_;
  std::unordered_map<std::uint32_t, std::uint32_t> mmio_regs_;
  std::uint32_t scu_interrupt_pending_ = 0U;
};

} // namespace saturnis::dev
