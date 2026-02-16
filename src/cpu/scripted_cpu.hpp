#pragma once

#include "bus/bus_arbiter.hpp"
#include "mem/memory.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace saturnis::cpu {

enum class ScriptOpKind { Read, Write, Compute, Barrier };

struct ScriptOp {
  ScriptOpKind kind = ScriptOpKind::Compute;
  std::uint32_t vaddr = 0;
  std::uint8_t size = 4;
  std::uint32_t value = 0;
  std::uint32_t cycles = 0;
};

struct PendingBusOp {
  bus::BusOp op{};
  std::size_t script_index = 0;
};

class ScriptedCPU {
public:
  ScriptedCPU(int cpu_id, std::vector<ScriptOp> script, std::size_t cache_line_size = 16, std::size_t cache_lines = 64);

  [[nodiscard]] bool done() const;
  [[nodiscard]] core::Tick local_time() const;
  [[nodiscard]] std::optional<PendingBusOp> produce();
  void apply_response(std::size_t script_index, const bus::BusResponse &response);
  [[nodiscard]] std::optional<std::uint32_t> last_read() const;

private:
  int cpu_id_;
  std::vector<ScriptOp> script_;
  std::size_t pc_ = 0;
  std::uint64_t sequence_ = 0;
  core::Tick local_time_ = 0;
  mem::StoreBuffer store_buffer_;
  mem::TinyCache cache_;
  std::optional<std::uint32_t> last_read_;
};

} // namespace saturnis::cpu
