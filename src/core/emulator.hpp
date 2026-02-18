#pragma once

#include "core/trace.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace saturnis::core {

struct RunConfig {
  bool headless = false;
  std::string bios_path;
  std::string trace_path;
  std::uint64_t max_steps = 20000;
  bool dual_demo = true;
};

class Emulator {
public:
  int run(const RunConfig &config);
  [[nodiscard]] std::string run_dual_demo_trace();
  [[nodiscard]] std::string run_dual_demo_trace_multithread();
  [[nodiscard]] std::string run_contention_stress_trace();
  [[nodiscard]] std::string run_contention_stress_trace_multithread();
  [[nodiscard]] std::string run_vdp1_source_event_stress_trace();
  [[nodiscard]] std::string run_vdp1_source_event_stress_trace_multithread();
  [[nodiscard]] std::string run_vdp1_source_event_stress_trace_cpu1_owner();
  [[nodiscard]] std::string run_vdp1_source_event_stress_trace_cpu1_owner_multithread();
  [[nodiscard]] std::string run_bios_trace(const std::vector<std::uint8_t> &bios_image, std::uint64_t max_steps = 20000);

private:
  void maybe_write_trace(const RunConfig &config, const TraceLog &trace) const;
};

} // namespace saturnis::core
