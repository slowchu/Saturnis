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
  [[nodiscard]] std::uint32_t sr() const;


private:
  void execute_instruction(std::uint16_t instr, core::TraceLog &trace, bool from_bus_commit);
  [[nodiscard]] bool t_flag() const;
  void set_t_flag(bool value);

  struct PendingMemOp {
    enum class Kind { ReadByte, WriteByte, ReadWord, WriteWord, ReadLong, WriteLong };
    Kind kind = Kind::ReadLong;
    std::uint32_t phys_addr = 0;
    std::uint8_t size = 4;
    std::uint32_t value = 0;
    std::uint32_t dst_reg = 0;
    std::optional<std::uint32_t> post_inc_reg;
    std::uint8_t post_inc_size = 0;
  };

  int cpu_id_;
  std::uint32_t pc_ = 0;
  std::uint32_t sr_ = 0;
  std::uint32_t pr_ = 0;
  std::uint32_t gbr_ = 0;
  std::array<std::uint32_t, 16> r_{};
  core::Tick t_ = 0;
  std::uint64_t executed_ = 0;
  std::optional<PendingMemOp> pending_mem_op_;
  std::optional<std::uint32_t> pending_branch_target_;
  mem::TinyCache icache_{16, 64};
};

} // namespace saturnis::cpu
