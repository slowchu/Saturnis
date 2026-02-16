#pragma once

#include "bus/bus_op.hpp"
#include "core/trace.hpp"
#include "dev/devices.hpp"
#include "mem/memory.hpp"

#include <cstdint>
#include <vector>

namespace saturnis::bus {

struct BusResponse {
  std::uint32_t value = 0;
  core::Tick stall = 0;
  core::Tick commit_time = 0;
  std::uint32_t line_base = 0;
  std::vector<std::uint8_t> line_data;
};

struct CommitResult {
  std::size_t input_index = 0;
  BusOp op{};
  BusResponse response{};
};

class BusArbiter {
public:
  BusArbiter(mem::CommittedMemory &memory, dev::DeviceHub &devices, core::TraceLog &trace);
  [[nodiscard]] BusResponse commit(const BusOp &op);
  [[nodiscard]] std::vector<CommitResult> commit_batch(const std::vector<BusOp> &ops);

private:
  [[nodiscard]] core::Tick base_latency(const BusOp &op) const;

  mem::CommittedMemory &memory_;
  dev::DeviceHub &devices_;
  core::TraceLog &trace_;
  core::Tick now_ = 0;
  std::uint32_t last_addr_ = 0;
};

} // namespace saturnis::bus
