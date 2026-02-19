#include "cpu/sh2_core.hpp"

namespace saturnis::cpu {
namespace {

constexpr std::uint32_t kSrTBit = 0x00000001U;

[[nodiscard]] constexpr std::uint32_t u32_add(std::uint32_t a, std::uint32_t b) {
  return a + b;
}

[[nodiscard]] constexpr std::uint32_t u32_sub(std::uint32_t a, std::uint32_t b) {
  return a - b;
}

[[nodiscard]] constexpr std::uint32_t u32_add_i32(std::uint32_t a, std::int32_t b) {
  return b >= 0 ? u32_add(a, static_cast<std::uint32_t>(b))
                : u32_sub(a, static_cast<std::uint32_t>(-static_cast<std::int64_t>(b)));
}

[[nodiscard]] constexpr std::int32_t signext12(std::uint32_t x) {
  const std::uint32_t v = x & 0x0FFFU;
  if ((v & 0x0800U) != 0U) {
    return static_cast<std::int32_t>(v | 0xFFFFF000U);
  }
  return static_cast<std::int32_t>(v);
}

[[nodiscard]] constexpr std::uint32_t u32_add_i64(std::uint32_t a, std::int64_t b) {
  return b >= 0 ? u32_add(a, static_cast<std::uint32_t>(b))
                : u32_sub(a, static_cast<std::uint32_t>(-b));
}

[[nodiscard]] constexpr std::int32_t signext8(std::uint32_t x) {
  return static_cast<std::int32_t>(static_cast<std::int8_t>(x & 0xFFU));
}

[[nodiscard]] constexpr bool add_overflow(std::uint32_t a, std::uint32_t b, std::uint32_t r) {
  return (((~(a ^ b)) & (a ^ r)) & 0x80000000U) != 0U;
}

[[nodiscard]] constexpr bool sub_overflow(std::uint32_t a, std::uint32_t b, std::uint32_t r) {
  return ((((a ^ b) & (a ^ r))) & 0x80000000U) != 0U;
}

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
  gbr_ = 0U;
  vbr_ = 0U;
  mach_ = 0U;
  macl_ = 0U;
  t_ = 0;
  executed_ = 0;
  pending_mem_op_.reset();
  pending_branch_target_.reset();
  pending_exception_vector_.reset();
  exception_return_pc_ = 0;
  exception_return_sr_ = 0;
  has_exception_return_context_ = false;
  pending_new_pc_ = 0;
  pending_new_sr_ = 0;
  pending_rte_restore_ = false;
  pending_trapa_imm_.reset();
}

void SH2Core::execute_instruction(std::uint16_t instr, core::TraceLog &trace, bool from_bus_commit) {
  // Minimal subset: NOP (0009), BRA disp12 (Axxx), MOV #imm,Rn (Ennn), ADD #imm,Rn (7nnn), ADD Rm,Rn (3nmC), MOV Rm,Rn (6nm3), RTS (000B)
  const auto delay_slot_target = pending_branch_target_;
  pending_branch_target_.reset();

  std::optional<std::uint32_t> next_branch_target;
  auto write_reg = [this](std::uint32_t n, std::uint32_t value) {
    r_[n] = value;
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
    const std::int32_t imm = signext8(instr);
    set_t_flag(r_[0] == static_cast<std::uint32_t>(imm));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x2008U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    set_t_flag((r_[n] & r_[m]) == 0U);
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xE000U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::int32_t imm = signext8(instr);
    write_reg(n, static_cast<std::uint32_t>(imm));
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0x7000U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::int32_t imm = signext8(instr);
    write_reg(n, u32_add_i32(r_[n], imm));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x300CU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, u32_add(r_[n], r_[m]));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x6003U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x2009U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[n] & r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x200AU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[n] ^ r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x200BU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[n] | r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x6007U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, ~r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x600BU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, u32_sub(0U, r_[m]));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x600CU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[m] & 0x000000FFU);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x600DU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, r_[m] & 0x0000FFFFU);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x600EU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    const std::uint8_t b = static_cast<std::uint8_t>(r_[m] & 0xFFU);
    write_reg(n, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(b))));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x600FU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    const std::uint16_t w = static_cast<std::uint16_t>(r_[m] & 0xFFFFU);
    write_reg(n, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(w))));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x3008U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    write_reg(n, u32_sub(r_[n], r_[m]));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x300AU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    const std::uint32_t carry_in = t_flag() ? 1U : 0U;
    const std::uint32_t lhs = r_[n];
    const std::uint32_t rhs = u32_add(r_[m], carry_in);
    const std::uint32_t out = u32_sub(lhs, rhs);
    set_t_flag(lhs < rhs);
    write_reg(n, out);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x300BU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    const std::uint32_t out = u32_sub(r_[n], r_[m]);
    set_t_flag(sub_overflow(r_[n], r_[m], out));
    write_reg(n, out);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x3002U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    set_t_flag(r_[n] >= r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x3003U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    set_t_flag(static_cast<std::int32_t>(r_[n]) >= static_cast<std::int32_t>(r_[m]));
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x3006U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    set_t_flag(r_[n] > r_[m]);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x3007U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    set_t_flag(static_cast<std::int32_t>(r_[n]) > static_cast<std::int32_t>(r_[m]));
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x4015U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    set_t_flag(static_cast<std::int32_t>(r_[n]) > 0);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x4011U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    set_t_flag(static_cast<std::int32_t>(r_[n]) >= 0);
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x200CU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    const std::uint32_t x = r_[n] ^ r_[m];
    set_t_flag(((x & 0xFFU) == 0U) || ((x & 0xFF00U) == 0U) || ((x & 0xFF0000U) == 0U) || ((x & 0xFF000000U) == 0U));
    pc_ += 2U;
  } else if ((instr & 0xFF00U) == 0xC900U) {
    r_[0] &= (instr & 0x00FFU);
    pc_ += 2U;
  } else if ((instr & 0xFF00U) == 0xCA00U) {
    r_[0] ^= (instr & 0x00FFU);
    pc_ += 2U;
  } else if ((instr & 0xFF00U) == 0xCB00U) {
    r_[0] |= (instr & 0x00FFU);
    pc_ += 2U;
  } else if ((instr & 0xFF00U) == 0x8900U) {
    const std::int32_t disp = signext8(instr) * 2;
    if (t_flag()) {
      pc_ = u32_add_i32(u32_add(pc_, 4U), disp);
    } else {
      pc_ += 2U;
    }
  } else if ((instr & 0xFF00U) == 0x8B00U) {
    const std::int32_t disp = signext8(instr) * 2;
    if (!t_flag()) {
      pc_ = u32_add_i32(u32_add(pc_, 4U), disp);
    } else {
      pc_ += 2U;
    }
  } else if ((instr & 0xFF00U) == 0x8D00U) {
    if (t_flag()) {
      next_branch_target = u32_add_i32(u32_add(pc_, 4U), signext8(instr) * 2);
    }
    pc_ += 2U;
  } else if ((instr & 0xFF00U) == 0x8F00U) {
    if (!t_flag()) {
      next_branch_target = u32_add_i32(u32_add(pc_, 4U), signext8(instr) * 2);
    }
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x0012U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    write_reg(n, gbr_);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x0022U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    write_reg(n, vbr_);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x401EU) {
    const std::uint32_t m = (instr >> 8U) & 0x0FU;
    gbr_ = r_[m];
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x400EU) {
    const std::uint32_t m = (instr >> 8U) & 0x0FU;
    sr_ = r_[m];
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x0002U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    write_reg(n, sr_);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x402EU) {
    const std::uint32_t m = (instr >> 8U) & 0x0FU;
    vbr_ = r_[m];
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x001AU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    write_reg(n, macl_);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x000AU) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    write_reg(n, mach_);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x401AU) {
    const std::uint32_t m = (instr >> 8U) & 0x0FU;
    macl_ = r_[m];
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x400AU) {
    const std::uint32_t m = (instr >> 8U) & 0x0FU;
    mach_ = r_[m];
    pc_ += 2U;
  } else if ((instr & 0xF00FU) == 0x0007U) {
    const std::uint32_t n = (instr >> 8U) & 0x0FU;
    const std::uint32_t m = (instr >> 4U) & 0x0FU;
    const std::int64_t prod = static_cast<std::int64_t>(static_cast<std::int32_t>(r_[n])) *
                              static_cast<std::int64_t>(static_cast<std::int32_t>(r_[m]));
    macl_ = static_cast<std::uint32_t>(prod & 0xFFFFFFFFULL);
    pc_ += 2U;
    t_ += 1;
  } else if ((instr & 0xFF00U) == 0xC700U) {
    r_[0] = (u32_add(pc_, 4U) & ~3U) + ((instr & 0xFFU) * 4U);
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
    const std::int64_t byte_offset = static_cast<std::int64_t>(signext12(instr)) * 2LL;
    next_branch_target = u32_add_i64(u32_add(branch_pc, 4U), byte_offset);
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xB000U) {
    const std::uint32_t branch_pc = pc_;
    const std::int64_t byte_offset = static_cast<std::int64_t>(signext12(instr)) * 2LL;
    pr_ = pc_ + 4U;
    next_branch_target = u32_add_i64(u32_add(branch_pc, 4U), byte_offset);
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x400BU) {
    const std::uint32_t m = (instr >> 8U) & 0x0FU;
    pr_ = pc_ + 4U;
    next_branch_target = r_[m];
    pc_ += 2U;
  } else if (instr == 0x000BU) {
    next_branch_target = pr_;
    pc_ += 2U;
  } else if ((instr & 0xF0FFU) == 0x402BU) {
    const std::uint32_t m = (instr >> 8U) & 0x0FU;
    next_branch_target = r_[m];
    pc_ += 2U;
  } else if (instr == 0x002BU) {
    if (!has_exception_return_context_) {
      trace.add_fault(core::FaultEvent{t_, cpu_id_, pc_, 0U, "SYNTHETIC_RTE_WITHOUT_CONTEXT"});
      pc_ += 2U;
    } else {
      pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::RtePopPc, mem::to_phys(r_[15]), 4U, 0U, 0U, std::nullopt, 0U, 0U};
      pc_ += 2U;
      trace.add_fault(core::FaultEvent{t_, cpu_id_, pc_, 0U, "EXCEPTION_RETURN"});
    }
  } else if ((instr & 0xFF00U) == 0xC300U) {
    pending_trapa_imm_ = (instr & 0xFFU);
    r_[15] = u32_sub(r_[15], 4U);
    const std::uint32_t addr = mem::to_phys(r_[15]);
    pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::TrapaPushSr, addr, 4U, sr_, 0U, std::nullopt, 0U, 0U};
  } else {
    trace.add_fault(core::FaultEvent{t_, cpu_id_, pc_, static_cast<std::uint32_t>(instr), "ILLEGAL_OP"});
    pc_ += 2U;
  }

  if (delay_slot_target.has_value()) {
    if (pending_rte_restore_) {
      sr_ = pending_new_sr_;
      pending_rte_restore_ = false;
    }
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


  // Exception contract: request_exception_vector() is taken at an instruction boundary where pc_
  // already points to the next instruction to execute. Entry therefore pushes SR then pc_ and vectors
  // via VBR, and RTE restores via stack after executing its architectural delay slot.
  if (pending_exception_vector_.has_value()) {
    trace.add_fault(core::FaultEvent{t_, cpu_id_, pc_, *pending_exception_vector_, "EXCEPTION_ENTRY"});
    r_[15] = u32_sub(r_[15], 4U);
    const std::uint32_t push_sr_addr = mem::to_phys(r_[15]);
    pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ExceptionPushSr, push_sr_addr, 4U, sr_, 0U, std::nullopt, 0U, 0U};
    pending_mem_op_->aux = *pending_exception_vector_;
    pending_exception_vector_.reset();
    out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(push_sr_addr, true), push_sr_addr, 4U, sr_};
    return out;
  }

  if (pending_mem_op_.has_value()) {
    const auto &pending = *pending_mem_op_;
    const bool is_write = pending.kind == PendingMemOp::Kind::WriteByte ||
                          pending.kind == PendingMemOp::Kind::WriteWord ||
                          pending.kind == PendingMemOp::Kind::WriteLong ||
                          pending.kind == PendingMemOp::Kind::ExceptionPushSr ||
                          pending.kind == PendingMemOp::Kind::ExceptionPushPc ||
                          pending.kind == PendingMemOp::Kind::TrapaPushSr ||
                          pending.kind == PendingMemOp::Kind::TrapaPushPc ||
                          pending.kind == PendingMemOp::Kind::RmwWriteByte;
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
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadByte, data_phys, 1U, 0U, n, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, 1U, 0U};
        return out;
      }
      if ((instr & 0xF000U) == 0x5000U) {
        n = (instr >> 8U) & 0x0FU;
        m = (instr >> 4U) & 0x0FU;
        const std::uint32_t disp = instr & 0x0FU;
        const std::uint32_t addr = mem::to_phys(r_[m] + (disp * 4U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadLong, addr, 4U, 0U, n, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 4U, 0U};
        return out;
      }
      if ((instr & 0xF000U) == 0x1000U) {
        n = (instr >> 8U) & 0x0FU;
        m = (instr >> 4U) & 0x0FU;
        const std::uint32_t disp = instr & 0x0FU;
        const std::uint32_t addr = mem::to_phys(r_[n] + (disp * 4U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteLong, addr, 4U, r_[m], 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, 4U, r_[m]};
        return out;
      }
      if ((instr & 0xF0FFU) == 0x4022U) {
        n = (instr >> 8U) & 0x0FU;
        r_[n] = u32_sub(r_[n], 4U);
        const std::uint32_t addr = mem::to_phys(r_[n]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteLong, addr, 4U, pr_, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, 4U, pr_};
        return out;
      }
      if ((instr & 0xF0FFU) == 0x4026U) {
        m = (instr >> 8U) & 0x0FU;
        const std::uint32_t addr = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadLong, addr, 4U, 0U, 0U, m, 4U, r_[m]};
        pending_mem_op_->aux = 1U;
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 4U, 0U};
        return out;
      }
      if ((instr & 0xFF00U) == 0x8500U) {
        m = (instr >> 4U) & 0x0FU;
        const std::uint32_t disp = instr & 0x0FU;
        const std::uint32_t addr = mem::to_phys(r_[m] + (disp * 2U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadWord, addr, 2U, 0U, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 2U, 0U};
        return out;
      }
      if ((instr & 0xFF00U) == 0x8400U) {
        m = (instr >> 4U) & 0x0FU;
        const std::uint32_t disp = instr & 0x0FU;
        const std::uint32_t addr = mem::to_phys(r_[m] + disp);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadByte, addr, 1U, 0U, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 1U, 0U};
        return out;
      }
      if (is_movw_mem_to_reg(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadWord, data_phys, 2U, 0U, n, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, 2U, 0U};
        return out;
      }
      if ((instr & 0xF000U) == 0x9000U) {
        n = (instr >> 8U) & 0x0FU;
        const std::uint32_t addr = mem::to_phys(u32_add(pc_, 4U) + ((instr & 0xFFU) * 2U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadWord, addr, 2U, 0U, n, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 2U, 0U};
        return out;
      }
      if ((instr & 0xFF00U) == 0xC400U) {
        const std::uint32_t addr = mem::to_phys(gbr_ + (instr & 0xFFU));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadByte, addr, 1U, 0U, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 1U, 0U};
        return out;
      }
      if ((instr & 0xFF00U) == 0xC500U) {
        const std::uint32_t addr = mem::to_phys(gbr_ + ((instr & 0xFFU) * 2U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadWord, addr, 2U, 0U, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 2U, 0U};
        return out;
      }
      if ((instr & 0xFF00U) == 0xC600U) {
        const std::uint32_t addr = mem::to_phys(gbr_ + ((instr & 0xFFU) * 4U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadLong, addr, 4U, 0U, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 4U, 0U};
        return out;
      }
      if ((instr & 0xF000U) == 0xD000U) {
        n = (instr >> 8U) & 0x0FU;
        const std::uint32_t addr = mem::to_phys((u32_add(pc_, 4U) & ~3U) + ((instr & 0xFFU) * 4U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadLong, addr, 4U, 0U, n, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 4U, 0U};
        return out;
      }
      if (is_movl_mem_to_reg(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ReadLong, data_phys, 4U, 0U, n, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, 4U, 0U};
        return out;
      }
      if ((instr & 0xF00FU) == 0x6004U || (instr & 0xF00FU) == 0x6005U || (instr & 0xF00FU) == 0x6006U) {
        n = (instr >> 8U) & 0x0FU; m = (instr >> 4U) & 0x0FU;
        const std::uint8_t sz = ((instr & 0x000FU) == 0x4U) ? 1U : (((instr & 0x000FU) == 0x5U) ? 2U : 4U);
        const auto kind = (sz == 1U) ? PendingMemOp::Kind::ReadByte : ((sz == 2U) ? PendingMemOp::Kind::ReadWord : PendingMemOp::Kind::ReadLong);
        const std::uint32_t data_phys = mem::to_phys(r_[m]);
        pending_mem_op_ = PendingMemOp{kind, data_phys, sz, 0U, n, m, sz, r_[m]};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, false), data_phys, sz, 0U};
        return out;
      }
      if ((instr & 0xF00FU) == 0x2000U) {
        n = (instr >> 8U) & 0x0FU; m = (instr >> 4U) & 0x0FU;
        const std::uint32_t data_phys = mem::to_phys(r_[n]);
        const std::uint32_t write_value = r_[m] & 0xFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteByte, data_phys, 1U, write_value, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, true), data_phys, 1U, write_value};
        return out;
      }
      if ((instr & 0xFF00U) == 0xC000U) {
        const std::uint32_t addr = mem::to_phys(gbr_ + (instr & 0xFFU));
        const std::uint32_t write_value = r_[0] & 0xFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteByte, addr, 1U, write_value, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, 1U, write_value};
        return out;
      }
      if ((instr & 0xFF00U) == 0xC100U) {
        const std::uint32_t addr = mem::to_phys(gbr_ + ((instr & 0xFFU) * 2U));
        const std::uint32_t write_value = r_[0] & 0xFFFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteWord, addr, 2U, write_value, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, 2U, write_value};
        return out;
      }
      if ((instr & 0xFF00U) == 0xC200U) {
        const std::uint32_t addr = mem::to_phys(gbr_ + ((instr & 0xFFU) * 4U));
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteLong, addr, 4U, r_[0], 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, 4U, r_[0]};
        return out;
      }
      if ((instr & 0xF00FU) == 0x8001U || (instr & 0xFF00U) == 0x8100U) {
        if ((instr & 0xF00FU) == 0x8001U) {
          n = (instr >> 8U) & 0x0FU;
        } else {
          n = (instr >> 4U) & 0x0FU;
        }
        const std::uint32_t disp = ((instr & 0xF00FU) == 0x8001U) ? ((instr >> 4U) & 0x0FU) : (instr & 0x0FU);
        const std::uint32_t addr = mem::to_phys(r_[n] + (disp * 2U));
        const std::uint32_t write_value = r_[0] & 0xFFFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteWord, addr, 2U, write_value, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, 2U, write_value};
        return out;
      }
      if ((instr & 0xF00FU) == 0x8000U || (instr & 0xFF00U) == 0x8000U) {
        if ((instr & 0xF00FU) == 0x8000U) {
          n = (instr >> 8U) & 0x0FU;
        } else {
          n = (instr >> 4U) & 0x0FU;
        }
        const std::uint32_t disp = ((instr & 0xF00FU) == 0x8000U) ? ((instr >> 4U) & 0x0FU) : (instr & 0x0FU);
        const std::uint32_t addr = mem::to_phys(r_[n] + disp);
        const std::uint32_t write_value = r_[0] & 0xFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteByte, addr, 1U, write_value, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, 1U, write_value};
        return out;
      }
      if ((instr & 0xF00FU) == 0x2004U || (instr & 0xF00FU) == 0x2005U || (instr & 0xF00FU) == 0x2006U) {
        n = (instr >> 8U) & 0x0FU; m = (instr >> 4U) & 0x0FU;
        const std::uint8_t sz = ((instr & 0x000FU) == 0x4U) ? 1U : (((instr & 0x000FU) == 0x5U) ? 2U : 4U);
        r_[n] = u32_sub(r_[n], sz);
        const std::uint32_t addr = mem::to_phys(r_[n]);
        const std::uint32_t write_value = (sz == 1U) ? (r_[m] & 0xFFU) : ((sz == 2U) ? (r_[m] & 0xFFFFU) : r_[m]);
        const auto kind = (sz == 1U) ? PendingMemOp::Kind::WriteByte : ((sz == 2U) ? PendingMemOp::Kind::WriteWord : PendingMemOp::Kind::WriteLong);
        pending_mem_op_ = PendingMemOp{kind, addr, sz, write_value, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, true), addr, sz, write_value};
        return out;
      }
      if ((instr & 0xFF00U) == 0xCC00U || (instr & 0xFF00U) == 0xCE00U || (instr & 0xFF00U) == 0xCF00U) {
        const std::uint32_t addr = mem::to_phys(gbr_ + r_[0]);
        const auto kind = ((instr & 0xFF00U) == 0xCC00U) ? PendingMemOp::Kind::RmwAndByteRead :
                          ((instr & 0xFF00U) == 0xCE00U) ? PendingMemOp::Kind::RmwXorByteRead : PendingMemOp::Kind::RmwOrByteRead;
        pending_mem_op_ = PendingMemOp{kind, addr, 1U, 0U, 0U, std::nullopt, 0U, 0U};
        pending_mem_op_->aux = instr & 0x00FFU;
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(addr, false), addr, 1U, 0U};
        return out;
      }
      if (is_movw_reg_to_mem(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[n]);
        const std::uint32_t write_value = r_[m] & 0xFFFFU;
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteWord, data_phys, 2U, write_value, 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, true), data_phys, 2U, write_value};
        return out;
      }
      if (is_movl_reg_to_mem(instr, n, m)) {
        const std::uint32_t data_phys = mem::to_phys(r_[n]);
        pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::WriteLong, data_phys, 4U, r_[m], 0U, std::nullopt, 0U, 0U};
        out.op = bus::BusOp{cpu_id_, t_, seq, data_access_kind(data_phys, true), data_phys, 4U, r_[m]};
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
    if (pending.kind == PendingMemOp::Kind::ExceptionPushSr) {
      r_[15] = u32_sub(r_[15], 4U);
      const std::uint32_t push_pc_addr = mem::to_phys(r_[15]);
      pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ExceptionPushPc, push_pc_addr, 4U, pc_, 0U, std::nullopt, 0U, 0U};
      pending_mem_op_->aux = pending.aux;
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    }
    if (pending.kind == PendingMemOp::Kind::ExceptionPushPc) {
      const std::uint32_t vector_phys = mem::to_phys(vbr_ + (pending.aux * 4U));
      pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::ExceptionVectorRead, vector_phys, 4U, 0U, 0U, std::nullopt, 0U, 0U};
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    }
    if (pending.kind == PendingMemOp::Kind::ExceptionVectorRead) {
      pc_ = response.value;
      has_exception_return_context_ = true;
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    }
    if (pending.kind == PendingMemOp::Kind::TrapaPushSr) {
      r_[15] = u32_sub(r_[15], 4U);
      const std::uint32_t addr = mem::to_phys(r_[15]);
      pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::TrapaPushPc, addr, 4U, u32_add(pc_, 2U), 0U, std::nullopt, 0U, 0U};
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    }
    if (pending.kind == PendingMemOp::Kind::TrapaPushPc) {
      const std::uint32_t vector_phys = mem::to_phys(vbr_ + ((*pending_trapa_imm_) * 4U));
      pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::TrapaVectorRead, vector_phys, 4U, 0U, 0U, std::nullopt, 0U, 0U};
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    }
    if (pending.kind == PendingMemOp::Kind::TrapaVectorRead) {
      pc_ = response.value;
      has_exception_return_context_ = true;
      pending_trapa_imm_.reset();
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    }
    if (pending.kind == PendingMemOp::Kind::RtePopPc) {
      pending_new_pc_ = response.value;
      r_[15] = u32_add(r_[15], 4U);
      pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::RtePopSr, mem::to_phys(r_[15]), 4U, 0U, 0U, std::nullopt, 0U, 0U};
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    }
    if (pending.kind == PendingMemOp::Kind::RtePopSr) {
      pending_new_sr_ = response.value;
      r_[15] = u32_add(r_[15], 4U);
      pending_rte_restore_ = true;
      pending_branch_target_ = pending_new_pc_;
      has_exception_return_context_ = false;
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    } else if (pending.kind == PendingMemOp::Kind::RmwAndByteRead || pending.kind == PendingMemOp::Kind::RmwXorByteRead || pending.kind == PendingMemOp::Kind::RmwOrByteRead) {
      const std::uint8_t read_byte = static_cast<std::uint8_t>(response.value & 0xFFU);
      std::uint8_t out_byte = read_byte;
      if (pending.kind == PendingMemOp::Kind::RmwAndByteRead) {
        out_byte = static_cast<std::uint8_t>(read_byte & static_cast<std::uint8_t>(pending.aux));
      } else if (pending.kind == PendingMemOp::Kind::RmwXorByteRead) {
        out_byte = static_cast<std::uint8_t>(read_byte ^ static_cast<std::uint8_t>(pending.aux));
      } else {
        out_byte = static_cast<std::uint8_t>(read_byte | static_cast<std::uint8_t>(pending.aux));
      }
      pending_mem_op_ = PendingMemOp{PendingMemOp::Kind::RmwWriteByte, pending.phys_addr, 1U, out_byte, 0U, std::nullopt, 0U, 0U};
      t_ += 1;
      ++executed_;
      trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
      return;
    } else if (pending.kind == PendingMemOp::Kind::ReadLong) {
      if (pending.aux == 1U) {
        pr_ = response.value;
      } else {
        r_[pending.dst_reg] = response.value;
      }
    } else if (pending.kind == PendingMemOp::Kind::ReadWord) {
      const std::uint16_t word = static_cast<std::uint16_t>(response.value & 0xFFFFU);
      r_[pending.dst_reg] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word)));
    } else if (pending.kind == PendingMemOp::Kind::ReadByte) {
      const std::uint8_t byte = static_cast<std::uint8_t>(response.value & 0xFFU);
      r_[pending.dst_reg] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(byte)));
    }

    if (pending.post_inc_reg.has_value() && *pending.post_inc_reg != pending.dst_reg) {
      r_[*pending.post_inc_reg] = u32_add(r_[*pending.post_inc_reg], pending.post_inc_size);
    }

    if (pending_branch_target_.has_value()) {
      if (pending_rte_restore_) {
        sr_ = pending_new_sr_;
        pending_rte_restore_ = false;
      }
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
    const std::uint32_t phys = mem::to_phys(pc_);
    const std::uint32_t expected_line_base = phys / static_cast<std::uint32_t>(icache_.line_size());
    if (response.line_base != expected_line_base || response.line_data.size() != icache_.line_size()) {
      trace.add_fault(core::FaultEvent{t_, cpu_id_, pc_, phys, "CACHE_FILL_MISMATCH"});
    } else {
      icache_.fill_line(response.line_base, response.line_data);
    }
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

void SH2Core::request_exception_vector(std::uint32_t vector) { pending_exception_vector_ = vector; }

void SH2Core::step(bus::BusArbiter &arbiter, core::TraceLog &trace, std::uint64_t seq) {
  const auto produced = produce_until_bus(seq, trace, 1);
  if (!produced.op.has_value()) {
    return;
  }
  const auto resp = arbiter.commit(*produced.op);
  apply_ifetch_and_step(resp, trace);
}


std::uint32_t SH2Core::pc() const { return pc_; }

core::Tick SH2Core::local_time() const { return t_; }

std::uint64_t SH2Core::executed_instructions() const { return executed_; }

std::uint32_t SH2Core::reg(std::size_t index) const { return r_[index & 0xFU]; }

std::uint32_t SH2Core::sr() const { return sr_; }

std::uint32_t SH2Core::pr() const { return pr_; }

std::uint32_t SH2Core::gbr() const { return gbr_; }

std::uint32_t SH2Core::vbr() const { return vbr_; }

std::uint32_t SH2Core::mach() const { return mach_; }

std::uint32_t SH2Core::macl() const { return macl_; }

void SH2Core::set_pr(std::uint32_t value) { pr_ = value; }

} // namespace saturnis::cpu
