#include "bus/bus_arbiter.hpp"

#include <algorithm>
#include <cassert>
#include <optional>

namespace saturnis::bus {


[[nodiscard]] bool valid_bus_size(std::uint8_t size) {
  return size == 1U || size == 2U || size == 4U;
}

[[nodiscard]] bool is_aligned(std::uint32_t addr, std::uint8_t size) {
  if (size == 1U) {
    return true;
  }
  return (addr % static_cast<std::uint32_t>(size)) == 0U;
}

[[nodiscard]] bool is_valid_bus_op(const BusOp &op) {
  if (op.kind == BusKind::Barrier) {
    return true;
  }
  if (!valid_bus_size(op.size)) {
    return false;
  }

  // Keep current SH-2 RAM subset behavior (which includes existing unaligned RAM tests) while
  // hardening externalized/observable bus operations.
  const bool require_alignment = op.kind == BusKind::MmioRead || op.kind == BusKind::MmioWrite ||
                                 op.kind == BusKind::IFetch || mem::is_mmio(op.phys_addr);
  return !require_alignment || is_aligned(op.phys_addr, op.size);
}


[[nodiscard]] std::size_t producer_slot(const BusOp &op) {
  if (op.producer == BusProducer::Dma || (op.producer == BusProducer::Auto && op.cpu_id < 0)) {
    return 2U;
  }
  if (op.cpu_id == 1) {
    return 1U;
  }
  return 0U;
}

PriorityClass DefaultArbitrationPolicy::priority_of(const BusOp &op) const {
  if (op.cpu_id < 0) {
    return PriorityClass::Dma;
  }
  if (op.kind == BusKind::MmioRead || op.kind == BusKind::MmioWrite || mem::is_mmio(op.phys_addr)) {
    return PriorityClass::CpuMmio;
  }
  return PriorityClass::CpuRam;
}

BusArbiter::BusArbiter(mem::CommittedMemory &memory, dev::DeviceHub &devices, core::TraceLog &trace,
                       const ArbitrationPolicy *policy, LatencyModel latency)
    : memory_(memory), devices_(devices), trace_(trace), policy_(policy ? policy : &default_policy_), latency_(latency) {}

bool BusArbiter::is_cpu(int cpu_id) const { return cpu_id == 0 || cpu_id == 1; }

core::Tick BusArbiter::base_latency(const BusOp &op) const {
  if (op.kind == BusKind::Barrier) {
    return latency_.barrier;
  }
  if (op.kind == BusKind::IFetch) {
    return latency_.ifetch;
  }
  if (op.kind == BusKind::MmioRead || (op.kind == BusKind::Read && mem::is_mmio(op.phys_addr))) {
    return latency_.mmio_read;
  }
  if (op.kind == BusKind::MmioWrite || (op.kind == BusKind::Write && mem::is_mmio(op.phys_addr))) {
    return latency_.mmio_write;
  }
  if (op.kind == BusKind::Write) {
    return latency_.ram_write;
  }
  return latency_.ram_read;
}

core::Tick BusArbiter::contention_extra(const BusOp &op, bool had_tie) const {
  core::Tick extra = 0;
  if (op.kind != BusKind::Barrier && has_last_addr_ && op.phys_addr == last_addr_) {
    extra += latency_.same_address_contention;
  }
  if (had_tie) {
    extra += latency_.tie_turnaround;
  }
  return extra;
}



BusResponse BusArbiter::fault_response(const BusOp &op, core::Tick start, const char *reason, std::uint32_t detail) {
  trace_.add_fault(core::FaultEvent{start, op.cpu_id, 0U, detail, reason});
  constexpr std::uint32_t kInvalidBusOpValue = 0xBAD0BAD0U;
  trace_.add_commit(core::CommitEvent{start, start, op, 0U, kInvalidBusOpValue, false});
  return BusResponse{kInvalidBusOpValue, 0U, start, start, 0U, {}};
}

bool BusArbiter::validate_enqueue_contract(const BusOp &op) {
  const auto slot = producer_slot(op);
  if (producer_enqueued_seen_[slot] && op.req_time < producer_last_enqueued_req_time_[slot]) {
    const core::Tick start = (op.req_time > bus_free_time_) ? op.req_time : bus_free_time_;
    (void)fault_response(op, start, "ENQUEUE_NON_MONOTONIC_REQ_TIME",
                         static_cast<std::uint32_t>(op.req_time & 0xFFFFFFFFU));
    return false;
  }
  producer_enqueued_seen_[slot] = true;
  producer_last_enqueued_req_time_[slot] = op.req_time;
  return true;
}
BusResponse BusArbiter::execute_commit(const BusOp &op, bool had_tie) {
  if (!is_valid_bus_op(op)) {
#ifndef NDEBUG
    assert(false && "invalid BusOp: size must be 1/2/4 and address must satisfy size alignment");
#endif
    const core::Tick start = (op.req_time > bus_free_time_) ? op.req_time : bus_free_time_;
    return fault_response(op, start, "INVALID_BUS_OP",
                          static_cast<std::uint32_t>((op.phys_addr & 0xFFFFU) | (static_cast<std::uint32_t>(op.size) << 24U)));
  }

  const auto slot = producer_slot(op);
  if (producer_seen_[slot] && op.req_time < producer_last_req_time_[slot]) {
    const core::Tick start = (op.req_time > bus_free_time_) ? op.req_time : bus_free_time_;
    return fault_response(op, start, "NON_MONOTONIC_REQ_TIME",
                          static_cast<std::uint32_t>(op.req_time & 0xFFFFFFFFU));
  }
  producer_seen_[slot] = true;
  producer_last_req_time_[slot] = op.req_time;

  const core::Tick start = (op.req_time > bus_free_time_) ? op.req_time : bus_free_time_;
  const core::Tick latency = base_latency(op) + contention_extra(op, had_tie);
  const core::Tick finish = start + latency;
  const core::Tick stall = finish - op.req_time;

  std::uint32_t value = op.data;
  std::uint32_t line_base = 0;
  std::vector<std::uint8_t> line_data;

  if (op.kind == BusKind::Barrier) {
    // Synchronization point: no memory or MMIO side effects.
  } else if (op.kind == BusKind::Write || op.kind == BusKind::MmioWrite) {
    if (mem::is_mmio(op.phys_addr) || op.kind == BusKind::MmioWrite) {
      devices_.write(finish, op.cpu_id, op.phys_addr, op.size, op.data);
    } else {
      memory_.write(op.phys_addr, op.size, op.data);
    }
  } else {
    if (mem::is_mmio(op.phys_addr) || op.kind == BusKind::MmioRead) {
      value = devices_.read(finish, op.cpu_id, op.phys_addr, op.size);
    } else {
      value = memory_.read(op.phys_addr, op.size);
      if (op.fill_cache_line && op.cache_line_size > 0U) {
        const auto lsize = static_cast<std::uint32_t>(op.cache_line_size);
        line_base = op.phys_addr / lsize;
        line_data = memory_.read_block(line_base * lsize, static_cast<std::size_t>(lsize));
      }
    }
  }

  if (op.kind != BusKind::Barrier) {
    last_addr_ = op.phys_addr;
    has_last_addr_ = true;
  }
  if (had_tie && is_cpu(op.cpu_id)) {
    last_grant_cpu_ = op.cpu_id;
  }

  bus_free_time_ = finish;
  trace_.add_commit(core::CommitEvent{start, finish, op, stall, value, false});
  return BusResponse{value, stall, start, finish, line_base, line_data};
}

bool BusArbiter::has_safe_horizon() const {
  if (!progress_tracking_enabled_) {
    return true;
  }
  // Once progress tracking is enabled, horizon gating stays closed until both
  // CPUs have published at least one executed_up_to progress watermark.
  return progress_up_to_[0] != std::numeric_limits<core::Tick>::max() &&
         progress_up_to_[1] != std::numeric_limits<core::Tick>::max();
}

core::Tick BusArbiter::commit_horizon() const {
  return std::min(progress_up_to_[0], progress_up_to_[1]);
}

void BusArbiter::update_progress(int cpu_id, core::Tick executed_up_to) {
  if (!is_cpu(cpu_id)) {
    return;
  }
  progress_tracking_enabled_ = true;
  auto &slot = progress_up_to_[static_cast<std::size_t>(cpu_id)];
  if (slot == std::numeric_limits<core::Tick>::max() || executed_up_to > slot) {
    slot = executed_up_to;
  }
}

std::size_t BusArbiter::pick_next(const std::vector<CommitResult> &pending, const std::vector<std::size_t> &committable) const {
  std::size_t best = committable.front();
  core::Tick best_start = std::max(pending[best].op.req_time, bus_free_time_);

  for (std::size_t idx : committable) {
    const auto &candidate = pending[idx].op;
    const core::Tick start = std::max(candidate.req_time, bus_free_time_);

    if (start < best_start) {
      best = idx;
      best_start = start;
      continue;
    }
    if (start > best_start) {
      continue;
    }

    const auto &cur = pending[best].op;
    if (producer_slot(candidate) == producer_slot(cur)) {
      if (candidate.req_time < cur.req_time) {
        best = idx;
      } else if (candidate.req_time == cur.req_time && candidate.sequence < cur.sequence) {
        best = idx;
      }
      continue;
    }

    const auto cprio = policy_->priority_of(candidate);
    const auto bprio = policy_->priority_of(cur);
    if (cprio > bprio) {
      best = idx;
      continue;
    }
    if (cprio < bprio) {
      continue;
    }

    if (is_cpu(candidate.cpu_id) && is_cpu(cur.cpu_id) && candidate.cpu_id != cur.cpu_id) {
      const int preferred = (last_grant_cpu_ == 0) ? 1 : 0;
      if (candidate.cpu_id == preferred) {
        best = idx;
      }
      continue;
    }

    if (candidate.cpu_id != cur.cpu_id) {
      if (candidate.cpu_id < cur.cpu_id) {
        best = idx;
      }
      continue;
    }
    if (candidate.sequence < cur.sequence) {
      best = idx;
    }
  }

  return best;
}

BusResponse BusArbiter::commit(const BusOp &op) {
  if (trace_.should_halt()) {
    constexpr std::uint32_t kInvalidBusOpValue = 0xBAD0BAD0U;
    return BusResponse{kInvalidBusOpValue, 0U, bus_free_time_, bus_free_time_, 0U, {}};
  }
  if (!validate_enqueue_contract(op)) {
    return BusResponse{0xBAD0BAD0U, 0U, bus_free_time_, bus_free_time_, 0U, {}};
  }
  return execute_commit(op, false);
}

BusResponse BusArbiter::commit_dma(BusOp op) {
  op.cpu_id = -1;
  op.producer = BusProducer::Dma;
  if (trace_.should_halt()) {
    constexpr std::uint32_t kInvalidBusOpValue = 0xBAD0BAD0U;
    return BusResponse{kInvalidBusOpValue, 0U, bus_free_time_, bus_free_time_, 0U, {}};
  }
  if (!validate_enqueue_contract(op)) {
    return BusResponse{0xBAD0BAD0U, 0U, bus_free_time_, bus_free_time_, 0U, {}};
  }
  return execute_commit(op, false);
}

std::vector<CommitResult> BusArbiter::commit_batch(const std::vector<BusOp> &ops) {
  producer_enqueued_seen_.fill(false);
  producer_last_enqueued_req_time_.fill(0U);

  std::vector<CommitResult> pending;
  pending.reserve(ops.size());
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (trace_.should_halt()) {
      break;
    }
    if (!validate_enqueue_contract(ops[i])) {
      continue;
    }
    pending.push_back(CommitResult{i, ops[i], {}});
  }
  std::vector<CommitResult> committed;
  committed.reserve(ops.size());

  while (!pending.empty()) {
    if (trace_.should_halt()) {
      break;
    }
    std::vector<std::size_t> committable;
    committable.reserve(pending.size());
    const core::Tick horizon = commit_horizon();
    for (std::size_t i = 0; i < pending.size(); ++i) {
      if (!progress_tracking_enabled_ || (has_safe_horizon() && pending[i].op.req_time < horizon)) {
        committable.push_back(i);
      }
    }
    if (committable.empty()) {
      break;
    }

    const std::size_t next_idx = pick_next(pending, committable);
    const core::Tick next_start = std::max(pending[next_idx].op.req_time, bus_free_time_);

    bool had_tie = false;
    for (std::size_t i : committable) {
      if (i == next_idx) {
        continue;
      }
      const auto &candidate = pending[i].op;
      const auto &chosen = pending[next_idx].op;
      const core::Tick candidate_start = std::max(candidate.req_time, bus_free_time_);
      if (candidate_start == next_start && policy_->priority_of(candidate) == policy_->priority_of(chosen)) {
        had_tie = true;
        break;
      }
    }

    auto chosen = pending[next_idx];
    chosen.response = execute_commit(chosen.op, had_tie);
    committed.push_back(chosen);
    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(next_idx));
    if (trace_.should_halt()) {
      break;
    }
  }

  return committed;
}

std::vector<CommitResult> BusArbiter::commit_pending(std::vector<BusOp> &pending_ops) {
  const auto committed = commit_batch(pending_ops);
  if (committed.empty()) {
    return committed;
  }

  std::vector<bool> was_committed(pending_ops.size(), false);
  for (const auto &result : committed) {
    if (result.input_index < was_committed.size()) {
      was_committed[result.input_index] = true;
    }
  }

  std::vector<BusOp> remaining;
  remaining.reserve(pending_ops.size() - committed.size());
  for (std::size_t i = 0; i < pending_ops.size(); ++i) {
    if (!was_committed[i]) {
      remaining.push_back(pending_ops[i]);
    }
  }
  pending_ops = std::move(remaining);
  return committed;
}

} // namespace saturnis::bus
