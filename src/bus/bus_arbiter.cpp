#include "bus/bus_arbiter.hpp"

#include <algorithm>
#include <optional>

namespace saturnis::bus {

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

BusResponse BusArbiter::execute_commit(const BusOp &op, bool had_tie) {
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
  return progress_up_to_[0] != std::numeric_limits<core::Tick>::max() ||
         progress_up_to_[1] != std::numeric_limits<core::Tick>::max();
}

core::Tick BusArbiter::commit_horizon() const {
  return std::min(progress_up_to_[0], progress_up_to_[1]);
}

void BusArbiter::update_progress(int cpu_id, core::Tick executed_up_to) {
  if (!is_cpu(cpu_id)) {
    return;
  }
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
    const auto cprio = policy_->priority_of(candidate);
    const auto bprio = policy_->priority_of(pending[best].op);

    if (start < best_start) {
      best = idx;
      best_start = start;
      continue;
    }
    if (start > best_start) {
      continue;
    }

    if (cprio > bprio) {
      best = idx;
      continue;
    }
    if (cprio < bprio) {
      continue;
    }

    const auto &cur = pending[best].op;
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

BusResponse BusArbiter::commit(const BusOp &op) { return execute_commit(op, false); }

std::vector<CommitResult> BusArbiter::commit_batch(const std::vector<BusOp> &ops) {
  std::vector<CommitResult> pending;
  pending.reserve(ops.size());
  for (std::size_t i = 0; i < ops.size(); ++i) {
    pending.push_back(CommitResult{i, ops[i], {}});
  }

  std::vector<CommitResult> committed;
  committed.reserve(ops.size());

  while (!pending.empty()) {
    std::vector<std::size_t> committable;
    committable.reserve(pending.size());
    const core::Tick horizon = commit_horizon();
    for (std::size_t i = 0; i < pending.size(); ++i) {
      if (!has_safe_horizon() || pending[i].op.req_time < horizon) {
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
  }

  return committed;
}

} // namespace saturnis::bus
