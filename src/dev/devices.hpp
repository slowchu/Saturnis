#pragma once

#include <cstdint>
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
};

} // namespace saturnis::dev
