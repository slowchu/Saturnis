#pragma once

#include "bus/bus_op.hpp"
#include "core/time.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace saturnis::core {

struct CpuSnapshot {
  Tick t = 0;
  int cpu = 0;
  std::uint32_t pc = 0;
  std::uint32_t sr = 0;
  std::array<std::uint32_t, 16> r{};
};

struct CommitEvent {
  Tick t_start = 0;
  Tick t_end = 0;
  bus::BusOp op{};
  Tick stall = 0;
  std::uint32_t value = 0;
  bool cache_hit = false;
};

class TraceLog {
public:
  void add_commit(const CommitEvent &event);
  void add_state(const CpuSnapshot &state);
  [[nodiscard]] std::string to_jsonl() const;
  void write_jsonl(std::ostream &os) const;

private:
  std::vector<std::string> lines_;
};

} // namespace saturnis::core
