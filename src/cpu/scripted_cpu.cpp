#include "cpu/scripted_cpu.hpp"

namespace saturnis::cpu {

ScriptedCPU::ScriptedCPU(int cpu_id, std::vector<ScriptOp> script, std::size_t cache_line_size, std::size_t cache_lines)
    : cpu_id_(cpu_id), script_(std::move(script)), cache_(cache_line_size, cache_lines) {}

bool ScriptedCPU::done() const { return pc_ >= script_.size(); }

core::Tick ScriptedCPU::local_time() const { return local_time_; }

std::optional<PendingBusOp> ScriptedCPU::produce() {
  while (!done()) {
    const auto &ins = script_[pc_];
    if (ins.kind == ScriptOpKind::Compute) {
      local_time_ += ins.cycles;
      ++pc_;
      continue;
    }
    if (ins.kind == ScriptOpKind::Barrier) {
      bus::BusOp op{cpu_id_, local_time_, sequence_++, bus::BusKind::Barrier, 0U, 0U, 0U};
      return PendingBusOp{op, pc_++};
    }

    const std::uint32_t phys = mem::to_phys(ins.vaddr);
    const bool uncached = mem::is_uncached_alias(ins.vaddr) || mem::is_mmio(phys);

    if (ins.kind == ScriptOpKind::Write) {
      const auto store_id = next_store_id_++;
      store_buffer_.push(mem::StoreEntry{store_id, phys, ins.size, ins.value});
      if (!uncached) {
        cache_.write(phys, ins.size, ins.value);
      }
      bus::BusKind kind = mem::is_mmio(phys) ? bus::BusKind::MmioWrite : bus::BusKind::Write;
      bus::BusOp op{cpu_id_, local_time_, sequence_++, kind, phys, ins.size, ins.value};
      op.producer_token = store_id;
      return PendingBusOp{op, pc_++};
    }

    if (!uncached) {
      if (const auto forwarded = store_buffer_.forward(phys, ins.size)) {
        last_read_ = *forwarded;
        ++pc_;
        continue;
      }
      std::uint32_t cache_value = 0;
      if (cache_.read(phys, ins.size, cache_value)) {
        last_read_ = cache_value;
        ++pc_;
        continue;
      }
    }

    bus::BusKind kind = mem::is_mmio(phys) ? bus::BusKind::MmioRead : bus::BusKind::Read;
    bus::BusOp op{cpu_id_, local_time_, sequence_++, kind, phys, ins.size, 0};
    if (!uncached) {
      op.fill_cache_line = true;
      op.cache_line_size = static_cast<std::uint8_t>(cache_.line_size());
    }
    return PendingBusOp{op, pc_++};
  }
  return std::nullopt;
}

void ScriptedCPU::apply_response(std::size_t script_index, const bus::BusResponse &response, std::uint64_t producer_token,
                                 core::TraceLog *trace) {
  local_time_ += response.stall;
  const auto &ins = script_[script_index];
  const std::uint32_t phys = mem::to_phys(ins.vaddr);
  if (ins.kind == ScriptOpKind::Write) {
    (void)store_buffer_.retire(producer_token);
    return;
  }

  if (ins.kind == ScriptOpKind::Read) {
    last_read_ = response.value;
    if (!(mem::is_uncached_alias(ins.vaddr) || mem::is_mmio(phys))) {
      if (!response.line_data.empty()) {
        const auto expected_line_base = phys / static_cast<std::uint32_t>(cache_.line_size());
        if (response.line_base != expected_line_base || response.line_data.size() != cache_.line_size()) {
          if (trace != nullptr) {
            trace->add_fault(core::FaultEvent{local_time_, cpu_id_, 0U, phys, "CACHE_FILL_MISMATCH"});
          }
          return;
        }
        cache_.fill_line(response.line_base, response.line_data);
      } else {
        const auto line_size = static_cast<std::uint32_t>(cache_.line_size());
        const std::uint32_t line_base = phys / line_size;
        cache_.fill_line(line_base, std::vector<std::uint8_t>(cache_.line_size(), 0U));
        cache_.write(phys, ins.size, response.value);
      }
    }
  }
}

std::optional<std::uint32_t> ScriptedCPU::last_read() const { return last_read_; }

std::size_t ScriptedCPU::store_buffer_size() const { return store_buffer_.size(); }

} // namespace saturnis::cpu
