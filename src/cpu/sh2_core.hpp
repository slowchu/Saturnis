#pragma once

#include "bus/bus_arbiter.hpp"
#include "core/trace.hpp"
#include "mem/memory.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace saturnis::cpu {

struct Sh2ProduceResult {
  std::optional<bus::BusOp> op;
  std::uint64_t executed = 0;
};

class SH2Core {
public:
  explicit SH2Core(int cpu_id);
  void reset(std::uint32_t pc, std::uint32_t sp);
  [[nodiscard]] Sh2ProduceResult produce_until_bus(std::uint64_t seq, core::TraceLog &trace,
                                                   std::uint32_t runahead_budget = 16);
  [[nodiscard]] bus::BusOp produce_ifetch(std::uint64_t seq) const;
  void apply_ifetch_and_step(const bus::BusResponse &response, core::TraceLog &trace);
  void step(bus::BusArbiter &arbiter, core::TraceLog &trace, std::uint64_t seq);
  [[nodiscard]] std::uint32_t pc() const;
  [[nodiscard]] core::Tick local_time() const;
  [[nodiscard]] std::uint64_t executed_instructions() const;

private:
  void execute_instruction(std::uint16_t instr, core::TraceLog &trace, bool from_bus_commit);

  int cpu_id_;
  std::uint32_t pc_ = 0;
  std::uint32_t sr_ = 0;
  std::array<std::uint32_t, 16> r_{};
  core::Tick t_ = 0;
  std::uint64_t executed_ = 0;
  mem::TinyCache icache_{16, 64};
};

} // namespace saturnis::cpu
