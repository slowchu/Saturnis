#pragma once

#include "core/trace.hpp"

#include <cstdint>
#include <string>

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

private:
  void maybe_write_trace(const RunConfig &config, const TraceLog &trace) const;
};

} // namespace saturnis::core
