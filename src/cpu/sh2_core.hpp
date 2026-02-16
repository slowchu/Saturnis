#pragma once

#include "bus/bus_arbiter.hpp"
#include "core/trace.hpp"
#include "mem/memory.hpp"

#include <array>
#include <cstddef>
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
  [[nodiscard]] std::uint32_t reg(std::size_t index) const;

private:
  enum class PendingKind { None, IFetch, DataRead, DataWrite };
  struct PendingBus {
    PendingKind kind = PendingKind::None;
    bus::BusOp op{};
    std::uint8_t dst_reg = 0;
    std::uint32_t addr = 0;
    std::uint8_t size = 4;
    bool cacheable = false;
  };

  [[nodiscard]] std::optional<bus::BusOp> execute_instruction(std::uint16_t instr, core::TraceLog &trace,
                                                               bool from_bus_commit);
  [[nodiscard]] bool decode_movl_store(std::uint16_t instr, std::uint8_t &n, std::uint8_t &m) const;
  [[nodiscard]] bool decode_movl_load(std::uint16_t instr, std::uint8_t &n, std::uint8_t &m) const;
  void retire_instruction(core::TraceLog &trace, bool from_bus_commit);
  [[nodiscard]] bool cacheable_data(std::uint32_t vaddr, std::uint32_t phys) const;

  int cpu_id_;
  std::uint32_t pc_ = 0;
  std::uint32_t sr_ = 0;
  std::array<std::uint32_t, 16> r_{};
  core::Tick t_ = 0;
  std::uint64_t executed_ = 0;
  mem::TinyCache icache_{16, 64};
  mem::TinyCache dcache_{16, 64};
  mem::StoreBuffer store_buffer_{};
  PendingBus pending_{};
};

} // namespace saturnis::cpu
