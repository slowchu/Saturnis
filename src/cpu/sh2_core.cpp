#include "cpu/sh2_core.hpp"

namespace saturnis::cpu {
namespace {

constexpr std::uint32_t kSrTBit = 0x00000001U;

[[nodiscard]] bool is_movl_mem_to_reg(std::uint16_t instr, std::uint32_t &n, std::uint32_t &m) {
  if ((instr & 0xF00FU) != 0x6002U) {
    return false;
  }
  n = (instr >> 8U) & 0x0FU;
  m = (instr >> 4U) & 0x0FU;
  return true;
}

[[nodiscard]] bool is_movl_reg_to_mem(std::uint16_t instr, std::uint32_t &n, std::uint32_t &m) {
  if ((instr & 0xF00FU) != 0x2002U) {
    return false;
  }
  n = (instr >> 8U) & 0x0FU;
  m = (instr >> 4U) & 0x0FU;
  return true;
}

[[nodiscard]] bool is_movw_mem_to_reg(std::uint16_t instr, std::uint32_t &n, std::uint32_t &m) {
  if ((instr & 0xF00FU) != 0x6001U) {
    return false;
  }
  n = (instr >> 8U) & 0x0FU;
  m = (instr >> 4U) & 0x0FU;
  return true;
}

[[nodiscard]] bool is_movw_reg_to_mem(std::uint16_t instr, std::uint32_t &n, std::uint32_t &m) {
  if ((instr & 0xF00FU) != 0x2001U) {
    return false;
  }
  n = (instr >> 8U) & 0x0FU;
  m = (instr >> 4U) & 0x0FU;
  return true;
}

[[nodiscard]] bus::BusKind data_access_kind(std::uint32_t phys_addr, bool is_write) {
  if (mem::is_mmio(phys_addr)) {
    return is_write ? bus::BusKind::MmioWrite : bus::BusKind::MmioRead;
  }
  return is_write ? bus::BusKind::Write : bus::BusKind::Read;
}

} // namespace

SH2Core::SH2Core(int cpu_id) : cpu_id_(cpu_id) {}

void SH2Core::reset(std::uint32_t pc, std::uint32_t sp) {
  pc_ = pc;
  r_[15] = sp;
  sr_ = 0xF0U;
  pr_ = 0U;
  t_ = 0;
  executed_ = 0;
  pending_mem_op_.reset();
  pending_branch_target_.reset();
}

void SH2Core::execute_instruction(std::uint16_t instr, core::TraceLog &trace, bool from_bus_commit) {
  // Minimal subset: NOP (0009), BRA disp12 (Axxx), MOV #imm,Rn (Ennn), ADD #imm,Rn (7nnn), ADD Rm,Rn (3nmC), MOV Rm,Rn (6nm3), RTS (000B)
  const auto delay_slot_target = pending_branch_target_;
  pending_branch_target_.reset();

  std::optional<std::uint32_t> next_branch_target;
  auto write_reg = [this](std::uint32_t n, std::uint32_t value) {
    r_[n] = value;
    if (n == 15U) {
      // TODO: Remove PR mirroring once LDS/STS and real call/return paths are implemented.
      pr_ = value;
    }
  };
  if (instr == 0x0009U) {
    pc_ += 2U;
  } else if (instr == 0x0018U) {
    set_t_flag(true);
    pc_ += 2U;
  } else if (instr == 0x0008U) {
    set_t_flag(false);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x0029U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    write_reg(n, t_flag() ? 1U : 0U);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x3000U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    set_t_flag(r_[n] == r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xFF00U) == 0x8800U) {
    const std::int32_t imm = static_cast<std::int8_t>(instr & 0xFFU);
    set_t_flag(r_[0] == static_cast<std::uint32_t>(imm));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x2008U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    set_t_flag((r_[n] & r_[m]) == 0U);
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xE000U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::int32_t imm = static_cast<std::int8_t>(instr & 0xFFU);
    write_reg(n, static_cast<std::uint32_t>(imm));
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0x7000U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::int32_t imm = static_cast<std::int8_t>(instr & 0xFFU);
    write_reg(n, r_[n] + static_cast<std::uint32_t>(imm));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x300CU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[n] + r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x6003U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x4000U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    set_t_flag((r_[n] & 0x80000000U) != 0U);
    write_reg(n, r_[n] << 1U);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x4001U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    set_t_flag((r_[n] & 0x1U) != 0U);
    write_reg(n, r_[n] >> 1U);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x4004U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const bool msb = (r_[n] & 0x80000000U) != 0U;
    set_t_flag(msb);
    write_reg(n, (r_[n] << 1U) | (msb ? 1U : 0U));
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x4005U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const bool lsb = (r_[n] & 0x1U) != 0U;
    set_t_flag(lsb);
    write_reg(n, (r_[n] >> 1U) | (lsb ? 0x80000000U : 0U));
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xA000U) {
    const std::uint32_t branch_pc = pc_;
    std::int32_t disp = static_cast<std::int32_t>(instr & 0x0FFFU);
    if ((disp & 0x800) != 0) {
      disp |= ~0xFFF;
    }
    const std::int32_t byte_offset = disp * 2;
    const std::int64_t target = static_cast<std::int64_t>(branch_pc) + 4 + static_cast<std::int64_t>(byte_offset);
    next_branch_target = static_cast<std::uint32_t>(target);
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xB000U) {
    const std::uint32_t branch_pc = pc_;
    std::int32_t disp = static_cast<std::int32_t>(instr & 0x0FFFU);
    if ((disp & 0x800) != 0) {
      disp |= ~0xFFF;
    }
    pr_ = branch_pc + 4U;
    next_branch_target = static_cast<std::uint32_t>(static_cast<std::int32_t>(branch_pc) + 4 + (disp << 1));
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x400BU) {
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    pr_ = pc_ + 4U;
    next_branch_target = r_[m];
    pc_ += 2U;
  } else if (instr == 0x000BU) {
    next_branch_target = pr_;
    pc_ += 2U;
  } else {
    // Unknown opcode treated as NOP for vertical-slice robustness.
    pc_ += 2U;
  }

  if (delay_slot_target.has_value()) {
    pc_ = *delay_slot_target;
  } else if (next_branch_target.has_value()) {
    pending_branch_target_ = *next_branch_target;
  }

  // Deterministic policy: when executing a delay slot, any branch target decoded in that slot is ignored;
  // the already-pending branch target wins (first-branch-wins semantics for this vertical slice).
  (void)from_bus_commit;
  t_ += 1; // intrinsic execute cost for each retired instruction.
  ++executed_;
  trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
}


Sh2ProduceResult SH2Core::produce_until_bus(std::uint64_t seq, core::TraceLog &trace, std::uint32_t runahead_budget) {
  Sh2ProduceResult out;

  if (pending_exception_vector_.has_value()) {
    const std::uint32_t vector_phys = mem::to_phys(static_cast<std::uint32_t>(*pending_exception_vector_) * 4U);
    pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ExceptionVectorRead, vector_phys, 4U, 0U, 0U};
    pending_exception_vector_.reset();
    out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(vector_phys, false), vector_phys, 4U, 0U};
    return out;
  }

  if (pending_mem_op_.has_value()) {
    const auto &pending = *pending_mem_op_;
    const bool is_write = pending.kind == PendingMemOp::Kind::WriteByte ||
                          pending.kind == PendingMemOp::Kind::WriteWord ||
                          pending.kind == PendingMemOp::Kind::WriteLong;
    out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(pending.phys_addr, is_write),
                        pending.phys_addr, pending.size, pending.value};
    return out;
  }

  for (std::uint32_t i = 0; i < runahead_budget; ++i) {
    const std::uint32_t phys = mem::to_phys(pc_);
    if (mem::is_uncached_alias(pc_) || mem::is_mmio(phys)) {
      out.op = bus::BusOp{cpu_id_, t_, seq, bus::BusKind::IFetch, phys, 2U, 0U, false, 0U};
      return out;
    }

    std::uint32_t cached = 0;
    if (icache_.read(phys, 2U, cached)) {
      const auto instr = static_cast<std::uint16_t>(cached & 0xFFFFU);
      std::uint32_t n = 0;
      std::uint32_t m = 0;
      if ((instr & 0xF00FU) == 0x6000U) {
        n = (instr >> 8U) & 0x0FU; m = (instr >> 4U) & 0x0FU;
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadByte, data_phys, 1U, 0U, n};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, 1U, 0U};
        return out;
      }
      if (is_movw_mem_to_reg(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadWord, data_phys, 2U, 0U, n};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, 2U, 0U};
        return out;
      }
      if (is_movl_mem_to_reg(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadLong, data_phys, 4U, 0U, n};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, 4U, 0U};
        return out;
      }
      if ((instr & 0xF00FU) == 0x6004U || (instr & 0xF00FU) == 0x6005U || (instr & 0xF00FU) == 0x6006U) {
        n = (instr >> 8U) & 0x0FU; m = (instr >> 4U) & 0x0FU;
        const std::uint8_t sz = ((instr & 0x000FU) == 0x4U) ? 1U : (((instr & 0x000FU) == 0x5U) ? 2U : 4U);
        const auto kind = (sz == 1U) ? PendingMemOp::Kind::ReadByte : ((sz == 2U) ? PendingMemOp::Kind::ReadWord : PendingMemOp::Kind::ReadLong);
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{kind, data_phys, sz, 0U, n, m, sz};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, sz, 0U};
        return out;
      }
      if ((instr & 0xF00FU) == 0x2000U) {
        n = (instr >> 8U) & 0x0FU; m = (instr >> 4U) & 0x0FU;
        const std::uint32_t data_phys = mem::to_phys(r_[n]);
        const std::uint32_t write_value = r_[m] & 0xFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteByte, data_phys, 1U, write_value, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, true), data_phys, 1U, write_value};
        return out;
      }
      if (is_movw_reg_to_mem(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[n]);
        const std::uint32_t write_value = r_[m] & 0xFFFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteWord, data_phys, 2U, write_value, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, true), data_phys, 2U, write_value};
        return out;
      }
      if (is_movl_reg_to_mem(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[n]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteLong, data_phys, 4U, r_[m], 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, true), data_phys, 4U, r_[m]};
        return out;
      }
      if (is_movw_mem_to_reg(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadWord, data_phys, 2U, 0U, n};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, 2U, 0U};
        return out;
      }
      if (is_movw_reg_to_mem(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[n]);
        const std::uint32_t write_value = r_[m] & 0xFFFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteWord, data_phys, 2U, write_value, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, true), data_phys, 2U, write_value};
        return out;
      }

      execute_instruction(instr, trace, false);
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
  t_ += response.stall;

  if (pending_mem_op_.has_value()) {
    const auto pending = *pending_mem_op_;
    pending_mem_op_.reset();
    if (pending.kind == PendingMemOp::Kind::ExceptionVectorRead) {
      exception_return_pc_ = pc_;
      exception_return_sr_ = sr_;
      pc_ = response.value;
    } else if (pending.kind == PendingMemOp::Kind::ReadLong) {
      r_[pending.dst_reg] = response.value;
      if (pending.dst_reg == 15U) {
        pr_ = r_[pending.dst_reg];
      }
    } else if (pending.kind == PendingMemOp::Kind::ReadWord) {
      const std::uint16_t word = static_cast<std::uint16_t>(response.value & 0xFFFFU);
      r_[pending.dst_reg] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word)));
      if (pending.dst_reg == 15U) {
        pr_ = r_[pending.dst_reg];
      }
    } else if (pending.kind == PendingMemOp::Kind::ReadByte) {
      const std::uint8_t byte = static_cast<std::uint8_t>(response.value & 0xFFU);
      r_[pending.dst_reg] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(byte)));
      if (pending.dst_reg == 15U) {
        pr_ = r_[pending.dst_reg];
      }
    }

    if (pending_branch_target_.has_value()) {
      pc_ = *pending_branch_target_;
      pending_branch_target_.reset();
    } else {
      pc_ += 2U;
    }

    t_ += 1;
    ++executed_;
    trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
    return;
  }

  if (!response.line_data.empty()) {
    icache_.fill_line(response.line_base, response.line_data);
  }
  const std::uint16_t instr = static_cast<std::uint16_t>(response.value & 0xFFFFU);
  execute_instruction(instr, trace, true);
}


bool SH2Core::t_flag() const { return (sr_ & kSrTBit) != 0U; }

void SH2Core::set_t_flag(bool value) {
  if (value) {
    sr_ |= kSrTBit;
  } else {
    sr_ &= ~kSrTBit;
  }
}

void SH2Core::step(bus::BusArbiter &arbiter, core::TraceLog &trace, std::uint64_t seq) {
  const auto produced = produce_until_bus(seq, trace, 1);
  if (!produced.op.has_value()) {
    return;
  }
  const auto resp = arbiter.commit(*produced.op);
  apply_ifetch_and_step(resp, trace);
}


bool SH2Core::t_flag() const { return (sr_ & kSrTBit) != 0U; }

void SH2Core::set_t_flag(bool value) {
  if (value) {
    sr_ |= kSrTBit;
  } else {
    sr_ &= ~kSrTBit;
  }
}

std::uint32_t SH2Core::pc() const { return pc_; }

core::Tick SH2Core::local_time() const { return t_; }

std::uint64_t SH2Core::executed_instructions() const { return executed_; }

std::uint32_t SH2Core::reg(std::size_t index) const { return r_[index & 0xFU]; }

std::uint32_t SH2Core::sr() const { return sr_; }

std::uint32_t SH2Core::pr() const { return pr_; }

void SH2Core::set_pr(std::uint32_t value) { pr_ = value; }

} // namespace saturnis::cpu
