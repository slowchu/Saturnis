#include "cpu/sh2_core.hpp"

namespace saturnis::cpu {

SH2Core::SH2Core(int cpu_id) : cpu_id_(cpu_id) {}

void SH2Core::reset(std::uint32_t pc, std::uint32_t sp) {
  pc_ = pc;
  r_[15] = sp;
  sr_ = 0xF0U;
  t_ = 0;
  executed_ = 0;
}

void SH2Core::execute_instruction(std::uint16_t instr, core::TraceLog &trace, bool from_bus_commit) {
  // Minimal subset: NOP (0009), BRA disp12 (Axxx), MOV #imm,Rn (Ennn), RTS (000B)
  if (instr == 0x0009U) {
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xE000U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::int32_t imm = static_cast<std::int8_t>(instr & 0xFFU);
    r_[n] = static_cast<std::uint32_t>(imm);
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xA000U) {
    std::int32_t disp = static_cast<std::int32_t>(instr & 0x0FFFU);
    if ((disp & 0x800) != 0) {
      disp |= ~0xFFF;
    }
    pc_ = static_cast<std::uint32_t>(static_cast<std::int32_t>(pc_) + 4 + (disp << 1));
  } else if (instr == 0x000BU) {
    pc_ = r_[15];
  } else {
    // Unknown opcode treated as NOP for vertical-slice robustness.
    pc_ += 2U;
  }

  if (!from_bus_commit) {
    t_ += 1; // local execute cost for icache hit.
  }
  ++executed_;
  trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
}

Sh2ProduceResult SH2Core::produce_until_bus(std::uint64_t seq, core::TraceLog &trace, std::uint32_t runahead_budget) {
  Sh2ProduceResult out;
  for (std::uint32_t i = 0; i < runahead_budget; ++i) {
    const std::uint32_t phys = mem::to_phys(pc_);
    if (mem::is_uncached_alias(pc_) || mem::is_mmio(phys)) {
      out.op = bus::BusOp{cpu_id_, t_, seq, bus::BusKind::IFetch, phys, 2U, 0U, false, 0U};
      return out;
    }

    std::uint32_t cached = 0;
    if (icache_.read(phys, 2U, cached)) {
      execute_instruction(static_cast<std::uint16_t>(cached & 0xFFFFU), trace, false);
      ++out.executed;
      continue;
    }

    bus::BusOp miss{cpu_id_, t_, seq, bus::BusKind::IFetch, phys, 2U, 0U};
    miss.fill_cache_line = true;
    miss.cache_line_size = static_cast<std::uint8_t>(icache_.line_size());
    out.op = miss;
    return out;
  }
  return out;
}

bus::BusOp SH2Core::produce_ifetch(std::uint64_t seq) const {
  const std::uint32_t phys = mem::to_phys(pc_);
  return bus::BusOp{cpu_id_, t_, seq, bus::BusKind::IFetch, phys, 2U, 0U};
}

void SH2Core::apply_ifetch_and_step(const bus::BusResponse &response, core::TraceLog &trace) {
  t_ = response.commit_time;
  if (!response.line_data.empty()) {
    icache_.fill_line(response.line_base, response.line_data);
  }
  const std::uint16_t instr = static_cast<std::uint16_t>(response.value & 0xFFFFU);
  execute_instruction(instr, trace, true);
}

void SH2Core::step(bus::BusArbiter &arbiter, core::TraceLog &trace, std::uint64_t seq) {
  const auto resp = arbiter.commit(produce_ifetch(seq));
  apply_ifetch_and_step(resp, trace);
}

std::uint32_t SH2Core::pc() const { return pc_; }

core::Tick SH2Core::local_time() const { return t_; }

std::uint64_t SH2Core::executed_instructions() const { return executed_; }

} // namespace saturnis::cpu
