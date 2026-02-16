#include "core/emulator.hpp"

#include <cstdlib>
#include <iostream>

int main() {
  saturnis::core::Emulator emu;
  const auto a = emu.run_dual_demo_trace();
  const auto b = emu.run_dual_demo_trace();
  if (a != b) {
    std::cerr << "trace regression mismatch\n";
    return 1;
  }
  std::cout << "trace regression stable\n";
  return 0;
}
