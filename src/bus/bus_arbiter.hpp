#pragma once

#include "bus/bus_op.hpp"
#include "core/trace.hpp"
#include "dev/devices.hpp"
#include "mem/memory.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace saturnis::bus {

struct BusResponse {
  std::uint32_t value = 0;
  core::Tick stall = 0;
  core::Tick start_time = 0;
  core::Tick commit_time = 0;
  std::uint32_t line_base = 0;
  std::vector<std::uint8_t> line_data;
};

struct CommitResult {
  std::size_t input_index = 0;
  BusOp op{};
  BusResponse response{};
};

enum class PriorityClass : std::uint8_t { CpuRam = 0, CpuMmio = 1, Dma = 2 };

class ArbitrationPolicy {
public:
  virtual ~ArbitrationPolicy() = default;
  [[nodiscard]] virtual PriorityClass priority_of(const BusOp &op) const = 0;
};

class DefaultArbitrationPolicy final : public ArbitrationPolicy {
public:
  [[nodiscard]] PriorityClass priority_of(const BusOp &op) const override;
};

struct LatencyModel {
  core::Tick ram_read = 4;
  core::Tick ram_write = 3;
  core::Tick ifetch = 5;
  core::Tick mmio_read = 12;
  core::Tick mmio_write = 10;
  core::Tick barrier = 1;

  core::Tick same_address_contention = 2;
  core::Tick tie_turnaround = 1;
};

class BusArbiter {
public:
  BusArbiter(mem::CommittedMemory &memory, dev::DeviceHub &devices, core::TraceLog &trace,
             const ArbitrationPolicy *policy = nullptr, LatencyModel latency = {});

  [[nodiscard]] BusResponse commit(const BusOp &op);
  [[nodiscard]] std::vector<CommitResult> commit_batch(const std::vector<BusOp> &ops);
  [[nodiscard]] std::vector<CommitResult> commit_pending(std::vector<BusOp> &pending_ops);

  void update_progress(int cpu_id, core::Tick executed_up_to);
  void mark_cpu_complete(int cpu_id);
  [[nodiscard]] core::Tick commit_horizon() const;

private:
  [[nodiscard]] bool is_cpu(int cpu_id) const;
  [[nodiscard]] core::Tick base_latency(const BusOp &op) const;
  [[nodiscard]] core::Tick contention_extra(const BusOp &op, bool had_tie) const;
  [[nodiscard]] bool has_safe_horizon() const;
  [[nodiscard]] std::size_t pick_next(const std::vector<CommitResult> &pending, const std::vector<std::size_t> &committable) const;
  [[nodiscard]] BusResponse execute_commit(const BusOp &op, bool had_tie);

  mem::CommittedMemory &memory_;
  dev::DeviceHub &devices_;
  core::TraceLog &trace_;

  DefaultArbitrationPolicy default_policy_{};
  const ArbitrationPolicy *policy_ = nullptr;
  LatencyModel latency_{};

  core::Tick bus_free_time_ = 0;
  int last_grant_cpu_ = 1;
  bool has_last_addr_ = false;
  std::uint32_t last_addr_ = 0;

  bool progress_tracking_enabled_ = false;
  std::array<bool, 2> progress_known_{{false, false}};
  std::array<bool, 2> cpu_complete_{{false, false}};
  std::array<core::Tick, 2> progress_up_to_{{0, 0}};
};

} // namespace saturnis::bus
