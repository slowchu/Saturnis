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

struct FaultEvent {
  Tick t = 0;
  int cpu = 0;
  std::uint32_t pc = 0;
  std::uint32_t detail = 0;
  std::string reason;
};

class TraceLog {
public:
  void set_halt_on_fault(bool enabled);
  [[nodiscard]] bool halt_on_fault() const;
  [[nodiscard]] bool should_halt() const;
  void add_commit(const CommitEvent &event);
  void add_state(const CpuSnapshot &state);
  void add_fault(const FaultEvent &fault);
  [[nodiscard]] std::string to_jsonl() const;
  void write_jsonl(std::ostream &os) const;

private:
  bool halt_on_fault_ = false;
  bool should_halt_ = false;
  std::vector<std::string> lines_;
};

} // namespace saturnis::core
