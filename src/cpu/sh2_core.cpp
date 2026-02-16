#include "cpu/sh2_core.hpp"

namespace saturnis::cpu {

SH2Core::SH2Core(int cpu_id) : cpu_id_(cpu_id) {}

void SH2Core::reset(std::uint32_t pc, std::uint32_t sp) {
  pc_ = pc;
  r_[15] = sp;
  sr_ = 0xF0U;
  t_ = 0;
  executed_ = 0;
  pending_ = PendingBus{};
}

bool SH2Core::decode_mov_store(std::uint16_t instr, std::uint8_t &n, std::uint8_t &m, std::uint8_t &size) const {
  if ((instr & 0xF000U) != 0x2000U) {
    return false;
  }
  const std::uint8_t low = static_cast<std::uint8_t>(instr & 0x0FU);
  if (low > 2U) {
    return false;
  }
  n = static_cast<std::uint8_t>((instr >> 8U) & 0x0FU);
  m = static_cast<std::uint8_t>((instr >> 4U) & 0x0FU);
  size = static_cast<std::uint8_t>(1U << low);
  return true;
}

bool SH2Core::decode_mov_load(std::uint16_t instr, std::uint8_t &n, std::uint8_t &m, std::uint8_t &size) const {
  if ((instr & 0xF000U) != 0x6000U) {
    return false;
  }
  const std::uint8_t low = static_cast<std::uint8_t>(instr & 0x0FU);
  if (low > 2U) {
    return false;
  }
  n = static_cast<std::uint8_t>((instr >> 8U) & 0x0FU);
  m = static_cast<std::uint8_t>((instr >> 4U) & 0x0FU);
  size = static_cast<std::uint8_t>(1U << low);
  return true;
}

bool SH2Core::decode_mov_store_predec(std::uint16_t instr, std::uint8_t &n, std::uint8_t &m, std::uint8_t &size) const {
  if ((instr & 0xF000U) != 0x2000U) {
    return false;
  }
  const std::uint8_t low = static_cast<std::uint8_t>(instr & 0x0FU);
  if (low < 4U || low > 6U) {
    return false;
  }
  n = static_cast<std::uint8_t>((instr >> 8U) & 0x0FU);
  m = static_cast<std::uint8_t>((instr >> 4U) & 0x0FU);
  size = static_cast<std::uint8_t>(1U << (low - 4U));
  return true;
}

bool SH2Core::decode_mov_load_postinc(std::uint16_t instr, std::uint8_t &n, std::uint8_t &m, std::uint8_t &size) const {
  if ((instr & 0xF000U) != 0x6000U) {
    return false;
  }
  const std::uint8_t low = static_cast<std::uint8_t>(instr & 0x0FU);
  if (low < 4U || low > 6U) {
    return false;
  }
  n = static_cast<std::uint8_t>((instr >> 8U) & 0x0FU);
  m = static_cast<std::uint8_t>((instr >> 4U) & 0x0FU);
  size = static_cast<std::uint8_t>(1U << (low - 4U));
  return true;
}

void SH2Core::retire_instruction(core::TraceLog &trace, bool from_bus_commit) {
  if (!from_bus_commit) {
    t_ += 1; // local execute cost for cache/local-view path.
  }
  ++executed_;
  trace.add_state(core::CpuSnapshot{t_, cpu_id_, pc_, sr_, r_});
}

bool SH2Core::cacheable_data(std::uint32_t vaddr, std::uint32_t phys) const {
  return !(mem::is_uncached_alias(vaddr) || mem::is_mmio(phys));
}

std::optional<bus::BusOp> SH2Core::execute_instruction(std::uint16_t instr, core::TraceLog &trace, bool from_bus_commit) {
  // Minimal subset: NOP (0009), BRA disp12 (Axxx), MOV #imm,Rn (Ennn), RTS (000B)
  std::uint8_t n = 0;
  std::uint8_t m = 0;
  std::uint8_t size = 0;
  if (decode_mov_store(instr, n, m, size)) {
    const std::uint32_t vaddr = r_[n];
    const std::uint32_t phys = mem::to_phys(vaddr);
    const bool cacheable = cacheable_data(vaddr, phys);
    const std::uint32_t value = r_[m];

    store_buffer_.push(mem::StoreEntry{phys, size, value});
    if (cacheable) {
      dcache_.write(phys, size, value);
    }

    bus::BusOp op{cpu_id_, t_, 0, mem::is_mmio(phys) ? bus::BusKind::MmioWrite : bus::BusKind::Write, phys, size, value};
    pending_ = PendingBus{PendingKind::DataWrite, op, 0U, phys, size, cacheable};
    pc_ += 2U;
    return op;
  }

  if (decode_mov_store_predec(instr, n, m, size)) {
    r_[n] -= size;
    const std::uint32_t vaddr = r_[n];
    const std::uint32_t phys = mem::to_phys(vaddr);
    const bool cacheable = cacheable_data(vaddr, phys);
    const std::uint32_t value = r_[m];
    store_buffer_.push(mem::StoreEntry{phys, size, value});
    if (cacheable) {
      dcache_.write(phys, size, value);
    }
    bus::BusOp op{cpu_id_, t_, 0, mem::is_mmio(phys) ? bus::BusKind::MmioWrite : bus::BusKind::Write, phys, size, value};
    pending_ = PendingBus{PendingKind::DataWrite, op, 0U, phys, size, cacheable};
    pc_ += 2U;
    return op;
  }

  if (decode_mov_load(instr, n, m, size) || decode_mov_load_postinc(instr, n, m, size)) {
    const bool post_inc = decode_mov_load_postinc(instr, n, m, size);
    const std::uint32_t vaddr = r_[m];
    const std::uint32_t phys = mem::to_phys(vaddr);
    const bool cacheable = cacheable_data(vaddr, phys);
    if (cacheable) {
      if (const auto fwd = store_buffer_.forward(phys, size)) {
        if (size == 1U) {
          r_[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(*fwd & 0xFFU)));
        } else if (size == 2U) {
          r_[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(*fwd & 0xFFFFU)));
        } else {
          r_[n] = *fwd;
        }
        if (post_inc) {
          r_[m] += size;
        }
        pc_ += 2U;
        retire_instruction(trace, from_bus_commit);
        return std::nullopt;
      }
      std::uint32_t cached = 0;
      if (dcache_.read(phys, size, cached)) {
        if (size == 1U) {
          r_[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(cached & 0xFFU)));
        } else if (size == 2U) {
          r_[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(cached & 0xFFFFU)));
        } else {
          r_[n] = cached;
        }
        if (post_inc) {
          r_[m] += size;
        }
        pc_ += 2U;
        retire_instruction(trace, from_bus_commit);
        return std::nullopt;
      }
    }

    bus::BusOp op{cpu_id_, t_, 0, mem::is_mmio(phys) ? bus::BusKind::MmioRead : bus::BusKind::Read, phys, size, 0U};
    if (cacheable) {
      op.fill_cache_line = true;
      op.cache_line_size = static_cast<std::uint8_t>(dcache_.line_size());
    }
    pending_ = PendingBus{PendingKind::DataRead,
                          op,
                          n,
                          phys,
                          size,
                          cacheable,
                          post_inc ? m : static_cast<std::uint8_t>(0xFFU),
                          post_inc ? size : static_cast<std::uint8_t>(0U)};
    return op;
  }

  if (instr == 0x0009U) {
    pc_ += 2U;
  } else if ((instr & 0xF000U) == 0xE000U) {
    const std::uint32_t reg_n = (instr >> 8U) & 0x0FU;
    const std::int32_t imm = static_cast<std::int8_t>(instr & 0xFFU);
    r_[reg_n] = static_cast<std::uint32_t>(imm);
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

  retire_instruction(trace, from_bus_commit);
  return std::nullopt;
}

Sh2ProduceResult SH2Core::produce_until_bus(std::uint64_t seq, core::TraceLog &trace, std::uint32_t runahead_budget) {
  Sh2ProduceResult out;
  if (pending_.kind != PendingKind::None) {
    pending_.op.sequence = seq;
    pending_.op.req_time = t_;
    out.op = pending_.op;
    return out;
  }

  for (std::uint32_t i = 0; i < runahead_budget; ++i) {
    const std::uint32_t phys = mem::to_phys(pc_);
    if (mem::is_uncached_alias(pc_) || mem::is_mmio(phys)) {
      pending_ = PendingBus{PendingKind::IFetch,
                            bus::BusOp{cpu_id_, t_, seq, bus::BusKind::IFetch, phys, 2U, 0U, false, 0U},
                            0U,
                            phys,
                            2U,
                            false};
      out.op = pending_.op;
      return out;
    }

    std::uint32_t cached = 0;
    if (icache_.read(phys, 2U, cached)) {
      const auto maybe_op = execute_instruction(static_cast<std::uint16_t>(cached & 0xFFFFU), trace, false);
      ++out.executed;
      if (maybe_op.has_value()) {
        pending_.op.sequence = seq;
        pending_.op.req_time = t_;
        out.op = pending_.op;
        return out;
      }
      continue;
    }

    bus::BusOp miss{cpu_id_, t_, seq, bus::BusKind::IFetch, phys, 2U, 0U};
    miss.fill_cache_line = true;
    miss.cache_line_size = static_cast<std::uint8_t>(icache_.line_size());
    pending_ = PendingBus{PendingKind::IFetch, miss, 0U, phys, 2U, false};
    out.op = pending_.op;
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
  switch (pending_.kind) {
  case PendingKind::IFetch: {
    if (!response.line_data.empty()) {
      icache_.fill_line(response.line_base, response.line_data);
    }
    const std::uint16_t instr = static_cast<std::uint16_t>(response.value & 0xFFFFU);
    pending_ = PendingBus{};
    (void)execute_instruction(instr, trace, true);
    break;
  }
  case PendingKind::DataRead: {
    if (pending_.size == 1U) {
      r_[pending_.dst_reg] =
          static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(response.value & 0xFFU)));
    } else if (pending_.size == 2U) {
      r_[pending_.dst_reg] =
          static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(response.value & 0xFFFFU)));
    } else {
      r_[pending_.dst_reg] = response.value;
    }
    if (pending_.cacheable && !response.line_data.empty()) {
      dcache_.fill_line(response.line_base, response.line_data);
    }
    if (pending_.post_inc_reg != 0xFFU) {
      r_[pending_.post_inc_reg] += pending_.post_inc_amount;
    }
    pc_ += 2U;
    pending_ = PendingBus{};
    retire_instruction(trace, true);
    break;
  }
  case PendingKind::DataWrite: {
    pending_ = PendingBus{};
    retire_instruction(trace, true);
    break;
  }
  case PendingKind::None:
    break;
  }
}

void SH2Core::step(bus::BusArbiter &arbiter, core::TraceLog &trace, std::uint64_t seq) {
  const auto resp = arbiter.commit(produce_ifetch(seq));
  apply_ifetch_and_step(resp, trace);
}

std::uint32_t SH2Core::pc() const { return pc_; }

core::Tick SH2Core::local_time() const { return t_; }

std::uint64_t SH2Core::executed_instructions() const { return executed_; }

std::uint32_t SH2Core::reg(std::size_t index) const { return r_[index & 0x0FU]; }

} // namespace saturnis::cpu
