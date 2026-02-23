#include "busarb/busarb.hpp"
#include "busarb/ymir_timing.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct TraceRecord {
  std::uint64_t seq = 0;
  std::string master;
  std::uint64_t tick_first_attempt = 0;
  std::uint64_t tick_complete = 0;
  std::uint32_t addr = 0;
  std::string addr_text;
  std::uint8_t size = 4;
  std::string rw;
  std::string kind;
  std::uint32_t service_cycles = 0;
  std::uint32_t retries = 0;
  std::string original_line;
  std::size_t source_line = 0;
};

struct ReplayResult {
  TraceRecord record{};
  std::uint32_t ymir_service_cycles = 0;
  std::uint32_t ymir_retries = 0;
  std::uint32_t ymir_effective_wait = 0;
  std::uint32_t ymir_effective_total = 0;
  std::string ymir_wait_metric_kind;
  std::uint32_t arbiter_wait = 0;
  std::uint32_t arbiter_service_cycles = 0;
  std::uint32_t arbiter_total = 0;
  std::int64_t delta_wait = 0;
  std::int64_t delta_total = 0;
  std::string classification;
  std::string known_gap_reason;
};

void print_help() {
  std::cout << "Usage: trace_replay <input.jsonl> [--annotated-output <path>] [--summary-output <path>] [--top <N>]\n"
            << "Schema: Phase 1 per-successful-access JSONL records.\n"
            << "Comparative replay only: keeps recorded Ymir ticks; does not retime downstream records.\n";
}

std::optional<std::string_view> find_value_span(std::string_view line, std::string_view key) {
  const std::string needle = std::string("\"") + std::string(key) + "\"";
  const auto key_pos = line.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = line.find(':', key_pos + needle.size());
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t start = colon + 1;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
    ++start;
  }

  if (start >= line.size()) {
    return std::nullopt;
  }

  if (line[start] == '"') {
    std::size_t end = start + 1;
    while (end < line.size() && line[end] != '"') {
      ++end;
    }
    if (end >= line.size()) {
      return std::nullopt;
    }
    return line.substr(start + 1, end - start - 1);
  }

  std::size_t end = start;
  while (end < line.size() && line[end] != ',' && line[end] != '}') {
    ++end;
  }
  return line.substr(start, end - start);
}

std::optional<std::uint64_t> parse_u64(std::string_view text) {
  std::uint64_t value = 0;
  const auto *begin = text.data();
  const auto *end = text.data() + text.size();
  auto result = std::from_chars(begin, end, value, 10);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint32_t> parse_addr(std::string_view text) {
  std::string_view trimmed = text;
  if (trimmed.rfind("0x", 0) == 0 || trimmed.rfind("0X", 0) == 0) {
    trimmed.remove_prefix(2);
  }
  std::uint32_t value = 0;
  auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value, 16);
  if (result.ec != std::errc{} || result.ptr != trimmed.data() + trimmed.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<TraceRecord> parse_record(const std::string &line, std::size_t source_line) {
  TraceRecord rec{};
  rec.original_line = line;
  rec.source_line = source_line;

  const auto seq = find_value_span(line, "seq");
  const auto master = find_value_span(line, "master");
  const auto tick_first_attempt = find_value_span(line, "tick_first_attempt");
  const auto tick_complete = find_value_span(line, "tick_complete");
  const auto addr = find_value_span(line, "addr");
  const auto size = find_value_span(line, "size");
  const auto rw = find_value_span(line, "rw");
  const auto kind = find_value_span(line, "kind");
  const auto service_cycles = find_value_span(line, "service_cycles");
  const auto retries = find_value_span(line, "retries");

  if (!seq || !master || !tick_first_attempt || !tick_complete || !addr || !size || !rw || !kind || !service_cycles || !retries) {
    return std::nullopt;
  }

  const auto parsed_seq = parse_u64(*seq);
  const auto parsed_tfa = parse_u64(*tick_first_attempt);
  const auto parsed_tc = parse_u64(*tick_complete);
  const auto parsed_addr = parse_addr(*addr);
  const auto parsed_size = parse_u64(*size);
  const auto parsed_service = parse_u64(*service_cycles);
  const auto parsed_retries = parse_u64(*retries);

  if (!parsed_seq || !parsed_tfa || !parsed_tc || !parsed_addr || !parsed_size || !parsed_service || !parsed_retries) {
    return std::nullopt;
  }

  rec.seq = *parsed_seq;
  rec.master = std::string(*master);
  rec.tick_first_attempt = *parsed_tfa;
  rec.tick_complete = *parsed_tc;
  rec.addr = *parsed_addr;
  rec.addr_text = std::string(*addr);
  rec.size = static_cast<std::uint8_t>(*parsed_size);
  rec.rw = std::string(*rw);
  rec.kind = std::string(*kind);
  rec.service_cycles = static_cast<std::uint32_t>(*parsed_service);
  rec.retries = static_cast<std::uint32_t>(*parsed_retries);
  return rec;
}

std::optional<busarb::BusMasterId> parse_master(const std::string &master) {
  if (master == "MSH2") {
    return busarb::BusMasterId::SH2_A;
  }
  if (master == "SSH2") {
    return busarb::BusMasterId::SH2_B;
  }
  if (master == "DMA") {
    return busarb::BusMasterId::DMA;
  }
  return std::nullopt;
}

std::string region_name(std::uint32_t addr) {
  if (addr <= 0x00FFFFFFU) return "BIOS ROM";
  if (addr >= 0x01000000U && addr <= 0x017FFFFFU) return "SMPC";
  if (addr >= 0x01800000U && addr <= 0x01FFFFFFU) return "Backup RAM";
  if (addr >= 0x02000000U && addr <= 0x02FFFFFFU) return "Low WRAM";
  if (addr >= 0x10000000U && addr <= 0x1FFFFFFFU) return "MINIT/SINIT";
  if (addr >= 0x20000000U && addr <= 0x4FFFFFFFU) return "A-Bus CS0/CS1";
  if (addr >= 0x50000000U && addr <= 0x57FFFFFFU) return "A-Bus dummy";
  if (addr >= 0x58000000U && addr <= 0x58FFFFFFU) return "CD Block CS2";
  if (addr >= 0x5A000000U && addr <= 0x5BFFFFFFU) return "SCSP";
  if (addr >= 0x5C000000U && addr <= 0x5C7FFFFFU) return "VDP1 VRAM";
  if (addr >= 0x5C800000U && addr <= 0x5CFFFFFFU) return "VDP1 FB";
  if (addr >= 0x5D000000U && addr <= 0x5D7FFFFFU) return "VDP1 regs";
  if (addr >= 0x5E000000U && addr <= 0x5FBFFFFFU) return "VDP2";
  if (addr >= 0x5FE00000U && addr <= 0x5FEFFFFFU) return "SCU regs";
  if (addr >= 0x60000000U && addr <= 0x7FFFFFFFU) return "High WRAM";
  return "Unmapped";
}

std::string json_escape(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_help();
    return 1;
  }

  std::string input_path;
  std::optional<std::string> annotated_output_path;
  std::optional<std::string> summary_output_path;
  std::size_t top_n = 20;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    }
    if (arg == "--annotated-output") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --annotated-output\n";
        return 1;
      }
      annotated_output_path = std::string(argv[++i]);
      continue;
    }
    if (arg == "--summary-output") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --summary-output\n";
        return 1;
      }
      summary_output_path = std::string(argv[++i]);
      continue;
    }
    if (arg == "--top") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --top\n";
        return 1;
      }
      const auto parsed = parse_u64(argv[++i]);
      if (!parsed) {
        std::cerr << "Invalid --top value\n";
        return 1;
      }
      top_n = static_cast<std::size_t>(*parsed);
      continue;
    }
    if (!arg.empty() && arg[0] != '-') {
      input_path = arg;
      continue;
    }
    std::cerr << "Unknown argument: " << arg << '\n';
    return 1;
  }

  if (input_path.empty()) {
    std::cerr << "Missing input trace file path\n";
    return 1;
  }

  std::ifstream input(input_path);
  if (!input.is_open()) {
    std::cerr << "Failed to open input file: " << input_path << '\n';
    return 1;
  }

  std::vector<TraceRecord> records;
  std::size_t malformed_lines = 0;
  std::size_t duplicate_seq_count = 0;
  std::size_t non_monotonic_seq_count = 0;
  std::set<std::uint64_t> seen_seq_values;
  std::optional<std::uint64_t> previous_seq_in_input;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    auto rec = parse_record(line, line_number);
    if (!rec.has_value()) {
      ++malformed_lines;
      std::cerr << "warning: malformed line " << line_number << " skipped\n";
      continue;
    }
    if (seen_seq_values.find(rec->seq) != seen_seq_values.end()) {
      ++duplicate_seq_count;
      std::cerr << "warning: duplicate seq " << rec->seq << " on line " << line_number << "\n";
    }
    seen_seq_values.insert(rec->seq);

    if (previous_seq_in_input.has_value() && rec->seq <= *previous_seq_in_input) {
      ++non_monotonic_seq_count;
      std::cerr << "warning: non-monotonic seq " << rec->seq << " on line " << line_number << "\n";
    }
    previous_seq_in_input = rec->seq;

    records.push_back(*rec);
  }

  std::stable_sort(records.begin(), records.end(), [](const TraceRecord &a, const TraceRecord &b) {
    if (a.tick_complete != b.tick_complete) {
      return a.tick_complete < b.tick_complete;
    }
    return a.seq < b.seq;
  });

  busarb::Arbiter arbiter({busarb::ymir_access_cycles, nullptr});
  std::vector<ReplayResult> results;
  results.reserve(records.size());

  std::size_t agreement_count = 0;
  std::size_t mismatch_count = 0;
  std::size_t known_gap_count = 0;
  std::size_t known_gap_byte_access_count = 0;
  std::map<std::string, std::size_t> histogram;

  for (const auto &record : records) {
    const auto master = parse_master(record.master);
    if (!master.has_value()) {
      ++malformed_lines;
      std::cerr << "warning: invalid master on line " << record.source_line << " skipped\n";
      continue;
    }

    const busarb::BusRequest req{*master, record.addr, record.rw == "W", record.size, record.tick_first_attempt};
    const auto wait = arbiter.query_wait(req);
    arbiter.commit_grant(req, record.tick_first_attempt);

    ReplayResult r{};
    r.record = record;
    r.ymir_service_cycles = record.service_cycles;
    r.ymir_retries = record.retries;
    const bool has_exact_ticks = record.tick_complete >= record.tick_first_attempt;
    if (has_exact_ticks) {
      r.ymir_effective_total = static_cast<std::uint32_t>(record.tick_complete - record.tick_first_attempt);
      r.ymir_effective_wait = (r.ymir_effective_total > r.ymir_service_cycles)
                                  ? (r.ymir_effective_total - r.ymir_service_cycles)
                                  : 0U;
      r.ymir_wait_metric_kind = "exact_tick_elapsed";
    } else {
      r.ymir_effective_wait = r.ymir_retries * r.ymir_service_cycles;
      r.ymir_effective_total = r.ymir_effective_wait + r.ymir_service_cycles;
      r.ymir_wait_metric_kind = "proxy_retries_x_service";
    }

    r.arbiter_wait = wait.wait_cycles;
    r.arbiter_service_cycles = std::max(1U, busarb::ymir_access_cycles(nullptr, record.addr, record.rw == "W", record.size));
    r.arbiter_total = r.arbiter_wait + r.arbiter_service_cycles;
    r.delta_wait = static_cast<std::int64_t>(r.arbiter_wait) - static_cast<std::int64_t>(r.ymir_effective_wait);
    r.delta_total = static_cast<std::int64_t>(r.arbiter_total) - static_cast<std::int64_t>(r.ymir_effective_total);

    const bool known_byte_gap = (record.size == 1U && r.ymir_retries == 0U && r.delta_wait > 0);
    if (known_byte_gap) {
      r.classification = "known_ymir_wait_model_gap";
      r.known_gap_reason = "byte_access_wait_check_gap";
      ++known_gap_count;
      ++known_gap_byte_access_count;
    } else if (r.delta_total == 0 && r.delta_wait == 0) {
      r.classification = "agreement";
      ++agreement_count;
    } else {
      r.classification = "mismatch";
      ++mismatch_count;
    }

    const std::string hist_key = region_name(record.addr) + " | " + r.classification;
    histogram[hist_key] += 1;
    results.push_back(r);
  }

  if (annotated_output_path.has_value()) {
    std::ofstream annotated(*annotated_output_path);
    if (!annotated.is_open()) {
      std::cerr << "Failed to open annotated output path: " << *annotated_output_path << '\n';
      return 1;
    }
    for (const auto &r : results) {
      annotated << "{"
                << "\"seq\":" << r.record.seq << ','
                << "\"master\":\"" << json_escape(r.record.master) << "\"," 
                << "\"tick_first_attempt\":" << r.record.tick_first_attempt << ','
                << "\"tick_complete\":" << r.record.tick_complete << ','
                << "\"addr\":\"" << json_escape(r.record.addr_text) << "\"," 
                << "\"size\":" << static_cast<unsigned>(r.record.size) << ','
                << "\"rw\":\"" << json_escape(r.record.rw) << "\"," 
                << "\"kind\":\"" << json_escape(r.record.kind) << "\"," 
                << "\"service_cycles\":" << r.record.service_cycles << ','
                << "\"retries\":" << r.record.retries << ','
                << "\"ymir_service_cycles\":" << r.ymir_service_cycles << ','
                << "\"ymir_retries\":" << r.ymir_retries << ','
                << "\"ymir_effective_wait\":" << r.ymir_effective_wait << ','
                << "\"ymir_effective_total\":" << r.ymir_effective_total << ','
                << "\"ymir_wait_metric_kind\":\"" << r.ymir_wait_metric_kind << "\"," 
                << "\"arbiter_wait\":" << r.arbiter_wait << ','
                << "\"arbiter_service_cycles\":" << r.arbiter_service_cycles << ','
                << "\"arbiter_total\":" << r.arbiter_total << ','
                << "\"delta_wait\":" << r.delta_wait << ','
                << "\"delta_total\":" << r.delta_total << ','
                << "\"classification\":\"" << r.classification << "\"," 
                << "\"known_gap_reason\":\"" << r.known_gap_reason << "\""
                << "}\n";
    }
  }

  if (summary_output_path.has_value()) {
    std::ofstream summary(*summary_output_path);
    if (!summary.is_open()) {
      std::cerr << "Failed to open summary output path: " << *summary_output_path << '\n';
      return 1;
    }

    std::vector<const ReplayResult *> sorted_for_summary;
    sorted_for_summary.reserve(results.size());
    for (const auto &r : results) {
      sorted_for_summary.push_back(&r);
    }
    std::stable_sort(sorted_for_summary.begin(), sorted_for_summary.end(), [](const ReplayResult *a, const ReplayResult *b) {
      return std::llabs(a->delta_total) > std::llabs(b->delta_total);
    });

    summary << "{\n";
    summary << "  \"records_processed\": " << results.size() << ",\n";
    summary << "  \"malformed_lines_skipped\": " << malformed_lines << ",\n";
    summary << "  \"duplicate_seq_count\": " << duplicate_seq_count << ",\n";
    summary << "  \"non_monotonic_seq_count\": " << non_monotonic_seq_count << ",\n";
    summary << "  \"agreement_count\": " << agreement_count << ",\n";
    summary << "  \"mismatch_count\": " << mismatch_count << ",\n";
    summary << "  \"known_gap_count\": " << known_gap_count << ",\n";
    summary << "  \"known_gap_byte_access_count\": " << known_gap_byte_access_count << ",\n";
    summary << "  \"delta_histogram\": {\n";
    std::size_t hist_index = 0;
    for (const auto &[key, count] : histogram) {
      summary << "    \"" << json_escape(key) << "\": " << count;
      ++hist_index;
      if (hist_index < histogram.size()) {
        summary << ',';
      }
      summary << "\n";
    }
    summary << "  },\n";
    summary << "  \"top_deltas\": [\n";
    const std::size_t emit_summary = std::min(top_n, sorted_for_summary.size());
    for (std::size_t i = 0; i < emit_summary; ++i) {
      const auto *r = sorted_for_summary[i];
      summary << "    {\"rank\": " << (i + 1)
              << ", \"seq\": " << r->record.seq
              << ", \"master\": \"" << json_escape(r->record.master) << "\""
              << ", \"addr\": \"" << json_escape(r->record.addr_text) << "\""
              << ", \"size\": " << static_cast<unsigned>(r->record.size)
              << ", \"delta_wait\": " << r->delta_wait
              << ", \"delta_total\": " << r->delta_total
              << ", \"classification\": \"" << json_escape(r->classification) << "\""
              << ", \"region\": \"" << json_escape(region_name(r->record.addr)) << "\"}";
      if (i + 1 < emit_summary) {
        summary << ',';
      }
      summary << "\n";
    }
    summary << "  ]\n";
    summary << "}\n";
  }

  std::cout << "records_processed: " << results.size() << "\n";
  std::cout << "malformed_lines_skipped: " << malformed_lines << "\n";
  std::cout << "duplicate_seq_count: " << duplicate_seq_count << "\n";
  std::cout << "non_monotonic_seq_count: " << non_monotonic_seq_count << "\n";
  std::cout << "agreement_count: " << agreement_count << "\n";
  std::cout << "mismatch_count: " << mismatch_count << "\n";
  std::cout << "known_gap_count: " << known_gap_count << "\n";
  std::cout << "known_gap_byte_access_count: " << known_gap_byte_access_count << "\n";
  std::cout << "delta_histogram:\n";
  for (const auto &[key, count] : histogram) {
    std::cout << "  " << key << " => " << count << "\n";
  }

  std::vector<const ReplayResult *> sorted;
  sorted.reserve(results.size());
  for (const auto &r : results) {
    sorted.push_back(&r);
  }
  std::stable_sort(sorted.begin(), sorted.end(), [](const ReplayResult *a, const ReplayResult *b) {
    return std::llabs(a->delta_total) > std::llabs(b->delta_total);
  });

  std::cout << "top_deltas:\n";
  const std::size_t emit = std::min(top_n, sorted.size());
  for (std::size_t i = 0; i < emit; ++i) {
    const auto *r = sorted[i];
    std::cout << "  #" << (i + 1)
              << " seq=" << r->record.seq
              << " master=" << r->record.master
              << " addr=" << r->record.addr_text
              << " size=" << static_cast<unsigned>(r->record.size)
              << " delta_wait=" << r->delta_wait
              << " delta_total=" << r->delta_total
              << " class=" << r->classification
              << " region=" << region_name(r->record.addr)
              << "\n";
  }

  return 0;
}
