#include "core/emulator.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> make_deterministic_bios_image() {
  // Minimal deterministic instruction stream:
  // 0x0000: MOV #0x40,R1
  // 0x0002: MOV.W @R1,R2
  // 0x0004: ADD #1,R2
  // 0x0006: MOV.L R2,@R1
  // 0x0008: NOP
  // 0x0040: initial data word 0xFF80
  std::vector<std::uint8_t> bios(0x80U, 0U);
  bios[0x00] = 0x40U;
  bios[0x01] = 0xE1U;
  bios[0x02] = 0x11U;
  bios[0x03] = 0x62U;
  bios[0x04] = 0x01U;
  bios[0x05] = 0x72U;
  bios[0x06] = 0x22U;
  bios[0x07] = 0x21U;
  bios[0x08] = 0x09U;
  bios[0x09] = 0x00U;
  bios[0x40] = 0x80U;
  bios[0x41] = 0xFFU;
  return bios;
}

bool trace_contains_checkpoint(const std::string &trace, const std::string &needle) {
  return trace.find(needle) != std::string::npos;
}

} // namespace

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

  const auto bios_image = make_deterministic_bios_image();
  const auto bios_fixture = emu.run_bios_trace(bios_image, 32U);
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (bios_trace != bios_fixture) {
      std::cerr << "bios bring-up trace fixture mismatch on run " << run << '\n';
      return 1;
    }
  }

  if (bios_fixture.find("\"kind\":\"IFETCH\"") == std::string::npos) {
    std::cerr << "bios bring-up trace missing IFETCH commit events\n";
    return 1;
  }
  if (bios_fixture.find("\"kind\":\"READ\"") == std::string::npos ||
      bios_fixture.find("\"kind\":\"WRITE\"") == std::string::npos) {
    std::cerr << "bios bring-up trace missing expected data read/write commits\n";
    return 1;
  }

  if (!trace_contains_checkpoint(bios_fixture, "\"cpu\":0,\"pc\":2,\"sr\":240,\"r\":[0,64") ||
      !trace_contains_checkpoint(bios_fixture, "\"cpu\":0,\"pc\":4,\"sr\":240,\"r\":[0,64,4294967168") ||
      !trace_contains_checkpoint(bios_fixture, "\"cpu\":0,\"pc\":6,\"sr\":240,\"r\":[0,64,4294967169") ||
      !trace_contains_checkpoint(bios_fixture, "\"cpu\":0,\"pc\":8,\"sr\":240,\"r\":[0,64,4294967169")) {
    std::cerr << "bios bring-up trace missing expected deterministic master-CPU state checkpoints\n";
    return 1;
  }

  if (!trace_contains_checkpoint(bios_fixture, "\"cpu\":1,\"pc\":2,\"sr\":240,\"r\":[0,64") ||
      !trace_contains_checkpoint(bios_fixture, "\"cpu\":1,\"pc\":4,\"sr\":240,\"r\":[0,64,4294967168") ||
      !trace_contains_checkpoint(bios_fixture, "\"cpu\":1,\"pc\":6,\"sr\":240,\"r\":[0,64,4294967169") ||
      !trace_contains_checkpoint(bios_fixture, "\"cpu\":1,\"pc\":8,\"sr\":240,\"r\":[0,64,4294967169")) {
    std::cerr << "bios bring-up trace missing expected deterministic slave-CPU state checkpoints\n";
    return 1;
  }

  std::cout << "trace regression stable\n";
  return 0;
}
