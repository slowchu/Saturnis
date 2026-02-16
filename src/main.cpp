#include "core/emulator.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  saturnis::core::RunConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--bios" && i + 1 < argc) {
      cfg.bios_path = argv[++i];
      cfg.dual_demo = false;
    } else if (arg == "--trace" && i + 1 < argc) {
      cfg.trace_path = argv[++i];
    } else if (arg == "--headless") {
      cfg.headless = true;
    } else if (arg == "--max-steps" && i + 1 < argc) {
      cfg.max_steps = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    } else if (arg == "--dual-demo") {
      cfg.dual_demo = true;
    } else if (arg == "--help") {
      std::cout << "Usage: saturnemu --bios <path> [--headless] [--trace trace.jsonl] [--max-steps N] [--dual-demo]\n";
      return 0;
    }
  }

  try {
    saturnis::core::Emulator emu;
    return emu.run(cfg);
  } catch (const std::exception &ex) {
    std::cerr << "Fatal: " << ex.what() << '\n';
    return 1;
  }
}
