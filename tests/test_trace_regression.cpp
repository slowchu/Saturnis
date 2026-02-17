#include "core/emulator.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> make_deterministic_bios_image() {
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

std::string first_line_containing(const std::string &haystack, const std::string &needle) {
  std::size_t line_start = 0;
  while (line_start < haystack.size()) {
    const std::size_t line_end = haystack.find('\n', line_start);
    const std::size_t end = (line_end == std::string::npos) ? haystack.size() : line_end;
    const auto line = haystack.substr(line_start, end - line_start);
    if (line.find(needle) != std::string::npos) {
      return line;
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  return {};
}

std::string commit_kind_sequence(const std::string &trace) {
  std::string out;
  std::size_t pos = 0;
  while ((pos = trace.find(R"("kind":")", pos)) != std::string::npos) {
    pos += 8;
    const std::size_t end = trace.find('"', pos);
    if (end == std::string::npos) {
      break;
    }
    out += trace.substr(pos, end - pos);
    out.push_back('|');
    pos = end + 1;
  }
  return out;
}

std::string first_n_commit_prefixes(const std::string &trace, std::size_t n) {
  std::string out;
  std::size_t line_start = 0;
  std::size_t seen = 0;
  while (line_start < trace.size() && seen < n) {
    const std::size_t line_end = trace.find('\n', line_start);
    const std::size_t end = (line_end == std::string::npos) ? trace.size() : line_end;
    const auto line = trace.substr(line_start, end - line_start);
    if (line.rfind("COMMIT ", 0) == 0) {
      out += line.substr(0, std::min<std::size_t>(line.size(), 48U));
      out.push_back('\n');
      ++seen;
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  return out;
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

  const std::size_t single_ifetch = count_occurrences(single_a, R"("kind":"IFETCH")");
  const std::size_t single_read = count_occurrences(single_a, R"("kind":"READ")");
  const std::size_t single_write = count_occurrences(single_a, R"("kind":"WRITE")");
  const std::size_t single_mmio_read = count_occurrences(single_a, R"("kind":"MMIO_READ")");
  const std::size_t single_mmio_write = count_occurrences(single_a, R"("kind":"MMIO_WRITE")");
  const std::size_t single_barrier = count_occurrences(single_a, R"("kind":"BARRIER")");

  const std::size_t mt_ifetch = count_occurrences(baseline_mt, R"("kind":"IFETCH")");
  const std::size_t mt_read = count_occurrences(baseline_mt, R"("kind":"READ")");
  const std::size_t mt_write = count_occurrences(baseline_mt, R"("kind":"WRITE")");
  const std::size_t mt_mmio_read = count_occurrences(baseline_mt, R"("kind":"MMIO_READ")");
  const std::size_t mt_mmio_write = count_occurrences(baseline_mt, R"("kind":"MMIO_WRITE")");
  const std::size_t mt_barrier = count_occurrences(baseline_mt, R"("kind":"BARRIER")");

  if (single_ifetch != mt_ifetch || single_read != mt_read || single_write != mt_write ||
      single_mmio_read != mt_mmio_read || single_mmio_write != mt_mmio_write || single_barrier != mt_barrier) {
    std::cerr << "single-thread and multithread dual-demo per-kind commit counts diverged\n";
    return 1;
  }

  const std::size_t single_src_read = count_occurrences(single_a, R"("src":"READ")");
  const std::size_t single_src_mmio = count_occurrences(single_a, R"("src":"MMIO")");
  const std::size_t single_src_barrier = count_occurrences(single_a, R"("src":"BARRIER")");
  const std::size_t mt_src_read = count_occurrences(baseline_mt, R"("src":"READ")");
  const std::size_t mt_src_mmio = count_occurrences(baseline_mt, R"("src":"MMIO")");
  const std::size_t mt_src_barrier = count_occurrences(baseline_mt, R"("src":"BARRIER")");
  if (single_src_read != mt_src_read || single_src_mmio != mt_src_mmio || single_src_barrier != mt_src_barrier) {
    std::cerr << "single-thread and multithread dual-demo src-field counts diverged\n";
    return 1;
  }

  const std::size_t single_cpu0_src_read = count_occurrences(single_a, R"("cpu":0,"src":"READ")");
  const std::size_t single_cpu1_src_read = count_occurrences(single_a, R"("cpu":1,"src":"READ")");
  const std::size_t mt_cpu0_src_read = count_occurrences(baseline_mt, R"("cpu":0,"src":"READ")");
  const std::size_t mt_cpu1_src_read = count_occurrences(baseline_mt, R"("cpu":1,"src":"READ")");
  if (single_cpu0_src_read != mt_cpu0_src_read || single_cpu1_src_read != mt_cpu1_src_read) {
    std::cerr << "single-thread and multithread per-CPU src-read counts diverged\n";
    return 1;
  }


  const auto single_cpu0_read_timing_line = first_line_containing(single_a, R"("cpu":0,"kind":"READ")");
  const auto single_cpu1_read_timing_line = first_line_containing(single_a, R"("cpu":1,"kind":"READ")");
  const auto mt_cpu0_read_timing_line = first_line_containing(baseline_mt, R"("cpu":0,"kind":"READ")");
  const auto mt_cpu1_read_timing_line = first_line_containing(baseline_mt, R"("cpu":1,"kind":"READ")");
  if ((!single_cpu0_read_timing_line.empty() && single_cpu0_read_timing_line != mt_cpu0_read_timing_line) ||
      (!single_cpu1_read_timing_line.empty() && single_cpu1_read_timing_line != mt_cpu1_read_timing_line)) {
    std::cerr << "single-thread and multithread per-CPU READ timing tuple lines diverged\n";
    return 1;
  }

  const std::size_t single_cpu0_src_mmio = count_occurrences(single_a, R"("cpu":0,"src":"MMIO")");
  const std::size_t single_cpu1_src_mmio = count_occurrences(single_a, R"("cpu":1,"src":"MMIO")");
  const std::size_t mt_cpu0_src_mmio = count_occurrences(baseline_mt, R"("cpu":0,"src":"MMIO")");
  const std::size_t mt_cpu1_src_mmio = count_occurrences(baseline_mt, R"("cpu":1,"src":"MMIO")");
  if (single_cpu0_src_mmio != mt_cpu0_src_mmio || single_cpu1_src_mmio != mt_cpu1_src_mmio) {
    std::cerr << "single-thread and multithread per-CPU src-mmio counts diverged\n";
    return 1;
  }


  const auto single_cpu0_mmio_timing_line = first_line_containing(single_a, R"("cpu":0,"src":"MMIO")");
  const auto single_cpu1_mmio_timing_line = first_line_containing(single_a, R"("cpu":1,"src":"MMIO")");
  const auto mt_cpu0_mmio_timing_line = first_line_containing(baseline_mt, R"("cpu":0,"src":"MMIO")");
  const auto mt_cpu1_mmio_timing_line = first_line_containing(baseline_mt, R"("cpu":1,"src":"MMIO")");
  if ((!single_cpu0_mmio_timing_line.empty() && single_cpu0_mmio_timing_line != mt_cpu0_mmio_timing_line) ||
      (!single_cpu1_mmio_timing_line.empty() && single_cpu1_mmio_timing_line != mt_cpu1_mmio_timing_line)) {
    std::cerr << "single-thread and multithread per-CPU MMIO timing tuple lines diverged\n";
    return 1;
  }

  const auto single_ifetch_line = first_line_containing(single_a, R"("kind":"IFETCH")");
  const auto mt_ifetch_line = first_line_containing(baseline_mt, R"("kind":"IFETCH")");
  if (!single_ifetch_line.empty() && single_ifetch_line != mt_ifetch_line) {
    std::cerr << "single-thread and multithread selected commit timing window diverged\n";
    return 1;
  }

  const std::size_t single_cache_hit_true = count_occurrences(single_a, R"("cache_hit":true)");
  const std::size_t single_cache_hit_false = count_occurrences(single_a, R"("cache_hit":false)");
  for (int run = 0; run < 5; ++run) {
    const auto single_trace = emu.run_dual_demo_trace();
    if (count_occurrences(single_trace, R"("cache_hit":true)") != single_cache_hit_true ||
        count_occurrences(single_trace, R"("cache_hit":false)") != single_cache_hit_false) {
      std::cerr << "dual-demo cache-hit counts changed on run " << run << '\n';
      return 1;
    }
  }

  const auto baseline_kind_order = commit_kind_sequence(single_a);
  for (int run = 0; run < 5; ++run) {
    const auto mt_trace = emu.run_dual_demo_trace_multithread();
    if (commit_kind_sequence(mt_trace) != baseline_kind_order) {
      std::cerr << "dual-demo mixed-kind commit sequence changed on multithread run " << run << '\n';
      return 1;
    }
  }

  const auto baseline_selected_timing_line = first_line_containing(single_a, R"("kind":"READ")");
  for (int run = 0; run < 5; ++run) {
    const auto mt_trace = emu.run_dual_demo_trace_multithread();
    const auto run_selected_timing_line = first_line_containing(mt_trace, R"("kind":"READ")");
    if (!baseline_selected_timing_line.empty() && run_selected_timing_line != baseline_selected_timing_line) {
      std::cerr << "dual-demo selected commit timing tuple changed on run " << run << '\n';
      return 1;
    }
  }


  const auto baseline_barrier_timing_line = first_line_containing(single_a, R"("kind":"BARRIER")");
  for (int run = 0; run < 5; ++run) {
    const auto mt_trace = emu.run_dual_demo_trace_multithread();
    const auto run_barrier_timing_line = first_line_containing(mt_trace, R"("kind":"BARRIER")");
    if (!baseline_barrier_timing_line.empty() && run_barrier_timing_line != baseline_barrier_timing_line) {
      std::cerr << "dual-demo selected BARRIER timing tuple changed on multithread run " << run << '\n';
      return 1;
    }
  }

  const auto baseline_prefixes = first_n_commit_prefixes(single_a, 12U);
  for (int run = 0; run < 5; ++run) {
    const auto mt_trace = emu.run_dual_demo_trace_multithread();
    if (first_n_commit_prefixes(mt_trace, 12U) != baseline_prefixes) {
      std::cerr << "dual-demo mixed-kind commit prefixes changed on multithread run " << run << '\n';
      return 1;
    }
  }


  const auto baseline_prefixes_24 = first_n_commit_prefixes(single_a, 24U);
  for (int run = 0; run < 5; ++run) {
    const auto mt_trace = emu.run_dual_demo_trace_multithread();
    if (first_n_commit_prefixes(mt_trace, 24U) != baseline_prefixes_24) {
      std::cerr << "dual-demo first 24 commit prefixes changed on multithread run " << run << '\n';
      return 1;
    }
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
    if (count_occurrences(bios_trace, R"("kind":"IFETCH")") != fixture_ifetch ||
        count_occurrences(bios_trace, R"("kind":"READ")") != fixture_read ||
        count_occurrences(bios_trace, R"("kind":"WRITE")") != fixture_write) {
      std::cerr << "bios IFETCH/READ/WRITE counts changed on run " << run << '\n';
      return 1;
    }
  }

  const std::size_t fixture_mmio_reads = count_occurrences(bios_fixture, R"("kind":"MMIO_READ")");
  const std::size_t fixture_mmio_writes = count_occurrences(bios_fixture, R"("kind":"MMIO_WRITE")");
  const std::size_t fixture_barrier = count_occurrences(bios_fixture, R"("kind":"BARRIER")");
  const std::size_t fixture_dma_tagged = count_occurrences(bios_fixture, R"("src":"DMA")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (count_occurrences(bios_trace, R"("kind":"MMIO_READ")") != fixture_mmio_reads ||
        count_occurrences(bios_trace, R"("kind":"MMIO_WRITE")") != fixture_mmio_writes ||
        count_occurrences(bios_trace, R"("kind":"BARRIER")") != fixture_barrier ||
        count_occurrences(bios_trace, R"("src":"DMA")") != fixture_dma_tagged) {
      std::cerr << "bios MMIO/BARRIER/DMA counts changed on run " << run << '\n';
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
    if (count_occurrences(bios_trace, R"("cache_hit":true)") != fixture_cache_hit_true ||
        count_occurrences(bios_trace, R"("cache_hit":false)") != fixture_cache_hit_false) {
      std::cerr << "bios cache-hit counts changed on run " << run << '\n';
      return 1;
    }
  }

  const std::size_t fixture_src_ifetch = count_occurrences(bios_fixture, R"("src":"IFETCH")");
  const std::size_t fixture_src_read = count_occurrences(bios_fixture, R"("src":"READ")");
  const std::size_t fixture_src_mmio = count_occurrences(bios_fixture, R"("src":"MMIO")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (count_occurrences(bios_trace, R"("src":"IFETCH")") != fixture_src_ifetch ||
        count_occurrences(bios_trace, R"("src":"READ")") != fixture_src_read ||
        count_occurrences(bios_trace, R"("src":"MMIO")") != fixture_src_mmio) {
      std::cerr << "bios src distribution changed on run " << run << '\n';
      return 1;
    }
  }

  const std::size_t fixture_cpu0_src_ifetch = count_occurrences(bios_fixture, R"("cpu":0,"src":"IFETCH")");
  const std::size_t fixture_cpu1_src_ifetch = count_occurrences(bios_fixture, R"("cpu":1,"src":"IFETCH")");
  const std::size_t fixture_cpu0_src_read = count_occurrences(bios_fixture, R"("cpu":0,"src":"READ")");
  const std::size_t fixture_cpu1_src_read = count_occurrences(bios_fixture, R"("cpu":1,"src":"READ")");
  const std::size_t fixture_cpu0_src_mmio = count_occurrences(bios_fixture, R"("cpu":0,"src":"MMIO")");
  const std::size_t fixture_cpu1_src_mmio = count_occurrences(bios_fixture, R"("cpu":1,"src":"MMIO")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (count_occurrences(bios_trace, R"("cpu":0,"src":"IFETCH")") != fixture_cpu0_src_ifetch ||
        count_occurrences(bios_trace, R"("cpu":1,"src":"IFETCH")") != fixture_cpu1_src_ifetch ||
        count_occurrences(bios_trace, R"("cpu":0,"src":"READ")") != fixture_cpu0_src_read ||
        count_occurrences(bios_trace, R"("cpu":1,"src":"READ")") != fixture_cpu1_src_read ||
        count_occurrences(bios_trace, R"("cpu":0,"src":"MMIO")") != fixture_cpu0_src_mmio ||
        count_occurrences(bios_trace, R"("cpu":1,"src":"MMIO")") != fixture_cpu1_src_mmio) {
      std::cerr << "bios per-CPU src distribution changed on run " << run << '\n';
      return 1;
    }
  }

  const std::size_t fixture_cpu0_ifetch = count_occurrences(bios_fixture, R"("cpu":0,"kind":"IFETCH")");
  const std::size_t fixture_cpu1_ifetch = count_occurrences(bios_fixture, R"("cpu":1,"kind":"IFETCH")");
  const std::size_t fixture_cpu0_read = count_occurrences(bios_fixture, R"("cpu":0,"kind":"READ")");
  const std::size_t fixture_cpu1_read = count_occurrences(bios_fixture, R"("cpu":1,"kind":"READ")");
  const std::size_t fixture_cpu0_write = count_occurrences(bios_fixture, R"("cpu":0,"kind":"WRITE")");
  const std::size_t fixture_cpu1_write = count_occurrences(bios_fixture, R"("cpu":1,"kind":"WRITE")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (count_occurrences(bios_trace, R"("cpu":0,"kind":"IFETCH")") != fixture_cpu0_ifetch ||
        count_occurrences(bios_trace, R"("cpu":1,"kind":"IFETCH")") != fixture_cpu1_ifetch ||
        count_occurrences(bios_trace, R"("cpu":0,"kind":"READ")") != fixture_cpu0_read ||
        count_occurrences(bios_trace, R"("cpu":1,"kind":"READ")") != fixture_cpu1_read ||
        count_occurrences(bios_trace, R"("cpu":0,"kind":"WRITE")") != fixture_cpu0_write ||
        count_occurrences(bios_trace, R"("cpu":1,"kind":"WRITE")") != fixture_cpu1_write) {
      std::cerr << "bios per-cpu commit-kind distribution changed on run " << run << '\n';
      return 1;
    }
  }


  const auto fixture_cpu0_ifetch_timing_line = first_line_containing(bios_fixture, R"("cpu":0,"kind":"IFETCH")");
  const auto fixture_cpu1_ifetch_timing_line = first_line_containing(bios_fixture, R"("cpu":1,"kind":"IFETCH")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    const auto run_cpu0_ifetch_timing_line = first_line_containing(bios_trace, R"("cpu":0,"kind":"IFETCH")");
    const auto run_cpu1_ifetch_timing_line = first_line_containing(bios_trace, R"("cpu":1,"kind":"IFETCH")");
    if ((!fixture_cpu0_ifetch_timing_line.empty() && run_cpu0_ifetch_timing_line != fixture_cpu0_ifetch_timing_line) ||
        (!fixture_cpu1_ifetch_timing_line.empty() && run_cpu1_ifetch_timing_line != fixture_cpu1_ifetch_timing_line)) {
      std::cerr << "bios per-CPU IFETCH timing tuple lines changed on run " << run << '\n';
      return 1;
    }
  }

  const auto fixture_mmio_write_line = first_line_containing(bios_fixture, R"("kind":"MMIO_WRITE")");
  const auto fixture_mmio_read_line = first_line_containing(bios_fixture, R"("kind":"MMIO_READ")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    if (!fixture_mmio_write_line.empty() && bios_trace.find(fixture_mmio_write_line) == std::string::npos) {
      std::cerr << "bios MMIO_WRITE timing tuple line changed on run " << run << '\n';
      return 1;
    }
    if (!fixture_mmio_read_line.empty() && bios_trace.find(fixture_mmio_read_line) == std::string::npos) {
      std::cerr << "bios MMIO_READ timing tuple line changed on run " << run << '\n';
      return 1;
    }
  }

  const std::size_t fixture_first_ifetch = bios_fixture.find(R"("kind":"IFETCH")");
  const std::size_t fixture_first_mmio_write = bios_fixture.find(R"("kind":"MMIO_WRITE")");
  const std::size_t fixture_first_mmio_read = bios_fixture.find(R"("kind":"MMIO_READ")");
  for (int run = 0; run < 5; ++run) {
    const auto bios_trace = emu.run_bios_trace(bios_image, 32U);
    const std::size_t run_ifetch = bios_trace.find(R"("kind":"IFETCH")");
    const std::size_t run_mmio_write = bios_trace.find(R"("kind":"MMIO_WRITE")");
    const std::size_t run_mmio_read = bios_trace.find(R"("kind":"MMIO_READ")");
    if ((fixture_first_ifetch != std::string::npos && fixture_first_mmio_write != std::string::npos) &&
        ((run_ifetch == std::string::npos) || (run_mmio_write == std::string::npos) ||
         ((run_ifetch < run_mmio_write) != (fixture_first_ifetch < fixture_first_mmio_write)))) {
      std::cerr << "bios MMIO ordering relative to IFETCH changed on run " << run << '\n';
      return 1;
    }
    if ((fixture_first_ifetch != std::string::npos && fixture_first_mmio_read != std::string::npos) &&
        ((run_ifetch == std::string::npos) || (run_mmio_read == std::string::npos) ||
         ((run_ifetch < run_mmio_read) != (fixture_first_ifetch < fixture_first_mmio_read)))) {
      std::cerr << "bios first MMIO_READ ordering relative to IFETCH changed on run " << run << '\n';
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
