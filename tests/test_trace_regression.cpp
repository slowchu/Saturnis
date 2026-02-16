#include "core/emulator.hpp"

#include <cstdlib>
#include <iostream>

int main() {
  saturnis::core::Emulator emu;

  const auto single_a = emu.run_dual_demo_trace();
  const auto single_b = emu.run_dual_demo_trace();
  if (single_a != single_b) {
    std::cerr << "single-thread trace regression mismatch\n";
    return 1;
  }

  std::string baseline_mt;
  for (int run = 0; run < 5; ++run) {
    const auto mt_trace = emu.run_dual_demo_trace_multithread();
    if (run == 0) {
      baseline_mt = mt_trace;
      continue;
    }
    if (mt_trace != baseline_mt) {
      std::cerr << "multithread trace regression mismatch on run " << run << '\n';
      return 1;
    }
  }

  if (baseline_mt != single_a) {
    std::cerr << "single-thread and multithread traces diverged\n";
    return 1;
  }

  std::cout << "trace regression stable\n";
  return 0;
}
