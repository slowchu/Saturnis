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

std::size_t count_occurrences(const std::string &haystack, const std::string &needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
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

  const std::size_t demo_barrier = count_occurrences(single_a, R"("kind":"BARRIER")");
  for (int run = 0; run < 5; ++run) {
    const auto mt_trace = emu.run_dual_demo_trace_multithread();
    if (count_occurrences(mt_trace, R"("kind":"BARRIER")") != demo_barrier) {
      std::cerr << "dual-demo BARRIER commit counts changed on multithread run " << run << '\n';
      return 1;
    }
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

  const std::size_t fixture_ifetch = count_occurrences(bios_fixture, R"("kind":"IFETCH")");
  const std::size_t fixture_read = count_occurrences(bios_fixture, R"("kind":"READ")");
  const std::size_t fixture_write = count_occurrences(bios_fixture, R"("kind":"WRITE")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (count_occurrences(bios_trace, R"("kind":"IFETCH")") != fixture_ifetch) {
      std::cerr << "bios fixture IFETCH commit counts changed on run " << run << '\n';
      return 1;
    }
    if (count_occurrences(bios_trace, R"("kind":"READ")") != fixture_read) {
      std::cerr << "bios fixture READ commit counts changed on run " << run << '\n';
      return 1;
    }
    if (count_occurrences(bios_trace, R"("kind":"WRITE")") != fixture_write) {
      std::cerr << "bios fixture WRITE commit counts changed on run " << run << '\n';
      return 1;
    }
  }

  const std::size_t fixture_mmio_reads = count_occurrences(bios_fixture, R"("kind":"MMIO_READ")");
  const std::size_t fixture_mmio_writes = count_occurrences(bios_fixture, R"("kind":"MMIO_WRITE")");
  const std::size_t fixture_barrier = count_occurrences(bios_fixture, R"("kind":"BARRIER")");
  const std::size_t fixture_dma_tagged = count_occurrences(bios_fixture, R"("src":"DMA")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (count_occurrences(bios_trace, R"("kind":"MMIO_READ")") != fixture_mmio_reads) {
      std::cerr << "bios fixture MMIO_READ commit counts changed on run " << run << '\n';
      return 1;
    }
    if (count_occurrences(bios_trace, R"("kind":"MMIO_WRITE")") != fixture_mmio_writes) {
      std::cerr << "bios fixture MMIO_WRITE commit counts changed on run " << run << '\n';
      return 1;
    }
    if (count_occurrences(bios_trace, R"("kind":"BARRIER")") != fixture_barrier) {
      std::cerr << "bios fixture BARRIER commit counts changed on run " << run << '\n';
      return 1;
    }
    if (count_occurrences(bios_trace, R"("src":"DMA")") != fixture_dma_tagged) {
      std::cerr << "bios fixture DMA-tagged commit counts changed on run " << run << '\n';
      return 1;
    }
  }

  if (fixture_dma_tagged != 0U) {
    std::cerr << "bios fixture unexpectedly contains DMA-tagged commits before DMA path modeling exists\n";
    return 1;
  }

  const std::size_t fixture_cache_hit_true = count_occurrences(bios_fixture, R"("cache_hit":true)");
  const std::size_t fixture_cache_hit_false = count_occurrences(bios_fixture, R"("cache_hit":false)");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (count_occurrences(bios_trace, R"("cache_hit":true)") != fixture_cache_hit_true) {
      std::cerr << "bios fixture cache-hit=true commit counts changed on run " << run << '\n';
      return 1;
    }
    if (count_occurrences(bios_trace, R"("cache_hit":false)") != fixture_cache_hit_false) {
      std::cerr << "bios fixture cache-hit=false commit counts changed on run " << run << '\n';
      return 1;
    }
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

  if (!trace_contains_checkpoint(bios_fixture,
                                 "\"t_start\":0,\"t_end\":6,\"stall\":6,\"cpu\":0,\"kind\":\"IFETCH\",\"phys\":0") ||
      !trace_contains_checkpoint(bios_fixture,
                                 "\"t_start\":6,\"t_end\":13,\"stall\":13,\"cpu\":1,\"kind\":\"IFETCH\",\"phys\":0")) {
    std::cerr << "bios bring-up trace missing expected deterministic commit timing checkpoints\n";
    return 1;
  }

  std::cout << "trace regression stable\n";
  return 0;
}
