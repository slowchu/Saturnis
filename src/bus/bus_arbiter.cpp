#include "bus/bus_arbiter.hpp"

#include <algorithm>

namespace saturnis::bus {

BusArbiter::BusArbiter(mem::CommittedMemory &memory, dev::DeviceHub &devices, core::TraceLog &trace)
    : memory_(memory), devices_(devices), trace_(trace) {}

core::Tick BusArbiter::base_latency(const BusOp &op) const {
  core::Tick latency = 2;
  if (mem::is_mmio(op.phys_addr)) {
    latency += 3;
  }
  if (op.kind == BusKind::Barrier) {
    return 1;
  }
  if (op.phys_addr == last_addr_) {
    latency += 1; // tiny contention model.
  }
  return latency;
}

BusResponse BusArbiter::commit(const BusOp &op) {
  const core::Tick start = (op.req_time > now_) ? op.req_time : now_;
  const core::Tick stall = base_latency(op);
  const core::Tick commit_t = start + stall;
  std::uint32_t value = op.data;
  std::uint32_t line_base = 0;
  std::vector<std::uint8_t> line_data;

  if (op.kind == BusKind::Barrier) {
    // Synchronization point: no memory transaction, deterministic stall only.
  } else if (op.kind == BusKind::Write || op.kind == BusKind::MmioWrite) {
    if (mem::is_mmio(op.phys_addr) || op.kind == BusKind::MmioWrite) {
      devices_.write(commit_t, op.cpu_id, op.phys_addr, op.size, op.data);
    } else {
      memory_.write(op.phys_addr, op.size, op.data);
    }
  } else {
    if (mem::is_mmio(op.phys_addr) || op.kind == BusKind::MmioRead) {
      value = devices_.read(commit_t, op.cpu_id, op.phys_addr, op.size);
    } else {
      value = memory_.read(op.phys_addr, op.size);
      if (op.fill_cache_line && op.cache_line_size > 0U) {
        const auto lsize = static_cast<std::uint32_t>(op.cache_line_size);
        line_base = op.phys_addr / lsize;
        line_data = memory_.read_block(line_base * lsize, static_cast<std::size_t>(lsize));
      }
    }
  }

  now_ = commit_t;
  if (op.kind != BusKind::Barrier) {
    last_addr_ = op.phys_addr;
  }
  trace_.add_commit(core::CommitEvent{commit_t, op, stall, value, false});
  return BusResponse{value, stall, commit_t, line_base, line_data};
}

std::vector<CommitResult> BusArbiter::commit_batch(const std::vector<BusOp> &ops) {
  std::vector<CommitResult> sorted;
  sorted.reserve(ops.size());
  for (std::size_t i = 0; i < ops.size(); ++i) {
    sorted.push_back(CommitResult{i, ops[i], {}});
  }

  std::sort(sorted.begin(), sorted.end(), [](const CommitResult &a, const CommitResult &b) {
    if (a.op.req_time != b.op.req_time) {
      return a.op.req_time < b.op.req_time;
    }
    if (a.op.cpu_id != b.op.cpu_id) {
      return a.op.cpu_id < b.op.cpu_id;
    }
    return a.op.sequence < b.op.sequence;
  });

  for (auto &entry : sorted) {
    entry.response = commit(entry.op);
  }
  return sorted;
}

} // namespace saturnis::bus
