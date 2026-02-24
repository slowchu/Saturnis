#include "busarb/busarb.hpp"
#include "busarb/ymir_timing.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

inline constexpr std::uint32_t kSummarySchemaVersion = 4;

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
  std::size_t source_line = 0;
};

struct ReplayResult {
  TraceRecord record{};

  std::uint32_t ymir_service_cycles = 0;
  std::uint32_t ymir_retries = 0;
  std::uint32_t ymir_elapsed = 0;
  std::uint32_t ymir_wait = 0;
  std::string ymir_wait_metric_kind;
  std::string cache_bucket;

  std::uint32_t model_predicted_service = 0;
  std::uint32_t model_predicted_wait = 0;
  std::uint32_t model_predicted_total = 0;

  std::uint32_t base_latency = 0;
  std::uint32_t contention_stall = 0;
  std::uint32_t total_predicted = 0;

  std::int64_t model_vs_trace_wait_delta = 0;
  std::int64_t model_vs_trace_total_delta = 0;

  std::int64_t cumulative_drift_wait = 0;
  std::int64_t cumulative_drift_total = 0;

  std::string classification;
  std::string known_gap_reason;
};

struct Options {
  std::string input_path;
  std::optional<std::string> annotated_output_path;
  std::optional<std::string> summary_output_path;
  std::size_t top_k = 20;
  bool summary_only = false;
  bool include_model_comparison = false;
  std::optional<std::size_t> annotated_limit;
};

void print_help() {
  std::cout << "Usage: trace_replay <input.{jsonl|bin}> [options]\n"
            << "  --annotated-output <path>   Write annotated JSONL\n"
            << "  --summary-output <path>     Write machine-readable summary JSON\n"
            << "  --summary-only              Skip annotated output even if path supplied\n"
            << "  --include-model-comparison  Enable arbiter/model-comparison metrics (hypothesis-only)\n"
            << "  --annotated-limit <N>       Emit first N annotated rows\n"
            << "  --top <N>                   Legacy alias for --top-k\n"
            << "  --top-k <N>                 Number of ranked entries to emit\n"
            << "  --help                      Show this help\n"
            << "Schema: Phase 1 per-successful-access JSONL records or BTR1 binary v1 records.\n"
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
  auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
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

  TraceRecord rec{};
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
  rec.source_line = source_line;
  return rec;
}

std::optional<busarb::BusMasterId> parse_master(const std::string &master) {
  if (master == "MSH2") return busarb::BusMasterId::SH2_A;
  if (master == "SSH2") return busarb::BusMasterId::SH2_B;
  if (master == "DMA") return busarb::BusMasterId::DMA;
  return std::nullopt;
}

std::string region_name(std::uint32_t addr) {
  if (addr <= 0x00FFFFFFU) return "BIOS ROM";
  if (addr >= 0x01000000U && addr <= 0x017FFFFFU) return "SMPC";
  if (addr >= 0x01800000U && addr <= 0x01FFFFFFU) return "Backup RAM";
  if (addr >= 0x02000000U && addr <= 0x02FFFFFFU) return "Low WRAM";
  if (addr >= 0x10000000U && addr <= 0x1FFFFFFFU) return "MINIT/SINIT";
  if (addr >= 0x20000000U && addr <= 0x4FFFFFFFU) return "A-Bus CS0/CS1";
  if (addr >= 0x05000000U && addr <= 0x057FFFFFU) return "A-Bus dummy";
  if (addr >= 0x05800000U && addr <= 0x058FFFFFU) return "CD Block CS2";
  if (addr >= 0x05A00000U && addr <= 0x05BFFFFFU) return "SCSP";
  if (addr >= 0x05C00000U && addr <= 0x05C7FFFFU) return "VDP1 VRAM";
  if (addr >= 0x05C80000U && addr <= 0x05CFFFFFU) return "VDP1 FB";
  if (addr >= 0x05D00000U && addr <= 0x05D7FFFFU) return "VDP1 regs";
  if (addr >= 0x05E00000U && addr <= 0x05FBFFFFU) return "VDP2";
  if (addr >= 0x05FE0000U && addr <= 0x05FEFFFFU) return "SCU regs";
  if (addr >= 0x06000000U && addr <= 0x07FFFFFFU) return "High WRAM";
  if (addr >= 0xFFFFFE00U && addr <= 0xFFFFFFFFU) return "SH-2 on-chip regs";
  return "Unmapped";
}

bool is_high_wram(std::uint32_t addr) {
  return addr >= 0x06000000U && addr <= 0x07FFFFFFU;
}

std::string classify_cache_bucket(std::uint32_t addr, std::uint32_t service_cycles) {
  if (!is_high_wram(addr)) {
    return "not_applicable";
  }
  if (service_cycles == 1U) {
    return "cache_hit";
  }
  if (service_cycles == 2U) {
    return "uncached_or_through";
  }
  if (service_cycles == 4U) {
    return "cache_miss_half";
  }
  if (service_cycles == 8U) {
    return "cache_miss_full";
  }
  return "anomaly";
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


std::uint32_t estimate_local_wait_cycles(const TraceRecord &record,
                                        const std::optional<TraceRecord> &previous_record,
                                        const busarb::ArbiterConfig &config) {
  if (!previous_record.has_value()) {
    return 0U;
  }

  std::uint32_t wait = 0U;
  if (previous_record->addr == record.addr) {
    wait += config.same_address_contention;
  }

  const auto prev_master = parse_master(previous_record->master);
  const auto cur_master = parse_master(record.master);
  const bool sh2_tie = prev_master.has_value() && cur_master.has_value() &&
                       previous_record->tick_first_attempt == record.tick_first_attempt &&
                       *prev_master != busarb::BusMasterId::DMA && *cur_master != busarb::BusMasterId::DMA &&
                       *prev_master != *cur_master;
  if (sh2_tie) {
    wait += config.tie_turnaround;
  }

  return wait;
}

std::string size_label(std::uint8_t size) {
  if (size == 1U) return "B";
  if (size == 2U) return "W";
  if (size == 4U) return "L";
  return std::to_string(size);
}

double percentile(std::vector<std::int64_t> values, double pct) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double index = pct * static_cast<double>(values.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(index));
  const auto hi = static_cast<std::size_t>(std::ceil(index));
  if (lo == hi) {
    return static_cast<double>(values[lo]);
  }
  const double frac = index - static_cast<double>(lo);
  return static_cast<double>(values[lo]) + (static_cast<double>(values[hi]) - static_cast<double>(values[lo])) * frac;
}


struct InputStats {
  std::size_t total_events = 0;
  std::size_t malformed_lines = 0;
  std::size_t duplicate_seq_count = 0;
  std::size_t non_monotonic_seq_count = 0;
};

struct ObservedBucketStats {
  std::size_t sample_size = 0;
  std::size_t observed_wait_nonzero_count = 0;
  std::vector<std::int64_t> observed_elapsed_values;
  std::vector<std::int64_t> observed_wait_values;
};

struct SymmetryCheckEntry {
  std::string bucket;
  ObservedBucketStats msh2;
  ObservedBucketStats ssh2;
  bool symmetric = true;
  std::string notes;
};

struct BinaryTraceRecordV1 {
  std::uint64_t seq;
  std::uint64_t tick_first_attempt;
  std::uint64_t tick_complete;
  std::uint64_t service_cycles;
  std::uint64_t retries;
  std::uint32_t addr;
  std::uint8_t size;
  std::uint8_t master;
  std::uint8_t rw;
  std::uint8_t kind;
};
static_assert(sizeof(BinaryTraceRecordV1) == 48, "BinaryTraceRecordV1 must be 48 bytes");

std::string to_hex32(std::uint32_t value) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string out = "0x00000000";
  for (int i = 0; i < 8; ++i) {
    out[9 - i] = kDigits[(value >> (i * 4)) & 0xF];
  }
  return out;
}

std::optional<std::string> decode_master(std::uint8_t value) {
  if (value == 0) return std::string("MSH2");
  if (value == 1) return std::string("SSH2");
  if (value == 2) return std::string("DMA");
  return std::nullopt;
}

std::optional<std::string> decode_rw(std::uint8_t value) {
  if (value == 0) return std::string("R");
  if (value == 1) return std::string("W");
  return std::nullopt;
}

std::optional<std::string> decode_kind(std::uint8_t value) {
  if (value == 0) return std::string("ifetch");
  if (value == 1) return std::string("read");
  if (value == 2) return std::string("write");
  if (value == 3) return std::string("mmio_read");
  if (value == 4) return std::string("mmio_write");
  return std::nullopt;
}

bool load_jsonl_records(const std::string &path, std::vector<TraceRecord> &records, InputStats &stats) {
  std::ifstream input(path);
  if (!input.is_open()) {
    std::cerr << "Failed to open input file: " << path << '\n';
    return false;
  }

  std::set<std::uint64_t> seen_seq_values;
  std::optional<std::uint64_t> previous_seq_in_input;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    ++stats.total_events;
    auto rec = parse_record(line, line_number);
    if (!rec.has_value()) {
      ++stats.malformed_lines;
      std::cerr << "warning: malformed line " << line_number << " skipped\n";
      continue;
    }
    if (seen_seq_values.find(rec->seq) != seen_seq_values.end()) {
      ++stats.duplicate_seq_count;
      std::cerr << "warning: duplicate seq " << rec->seq << " on line " << line_number << '\n';
    }
    seen_seq_values.insert(rec->seq);

    if (previous_seq_in_input.has_value() && rec->seq <= *previous_seq_in_input) {
      ++stats.non_monotonic_seq_count;
      std::cerr << "warning: non-monotonic seq " << rec->seq << " on line " << line_number << '\n';
    }
    previous_seq_in_input = rec->seq;
    records.push_back(*rec);
  }
  return true;
}

bool load_binary_records(const std::string &path, std::vector<TraceRecord> &records, InputStats &stats) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    std::cerr << "Failed to open input file: " << path << '\n';
    return false;
  }

  std::array<char, 8> header{};
  input.read(header.data(), static_cast<std::streamsize>(header.size()));
  if (input.gcount() != static_cast<std::streamsize>(header.size())) {
    std::cerr << "error: truncated binary header\n";
    return false;
  }
  if (!(header[0] == 'B' && header[1] == 'T' && header[2] == 'R' && header[3] == '1')) {
    std::cerr << "error: invalid binary magic (expected BTR1)\n";
    return false;
  }
  const std::uint16_t version = static_cast<std::uint8_t>(header[4]) | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(header[5])) << 8);
  const std::uint16_t record_size = static_cast<std::uint8_t>(header[6]) | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(header[7])) << 8);
  if (version != 1) {
    std::cerr << "error: unsupported binary trace version " << version << " (expected 1)\n";
    return false;
  }
  if (record_size != sizeof(BinaryTraceRecordV1)) {
    std::cerr << "error: unsupported binary record size " << record_size << " (expected " << sizeof(BinaryTraceRecordV1) << ")\n";
    return false;
  }

  std::set<std::uint64_t> seen_seq_values;
  std::optional<std::uint64_t> previous_seq;
  std::size_t index = 0;
  while (true) {
    BinaryTraceRecordV1 raw{};
    input.read(reinterpret_cast<char *>(&raw), sizeof(raw));
    const auto got = input.gcount();
    if (got == 0) {
      break;
    }
    if (got != static_cast<std::streamsize>(sizeof(raw))) {
      std::cerr << "error: truncated binary record at index " << index << "\n";
      return false;
    }
    ++stats.total_events;

    auto master = decode_master(raw.master);
    auto rw = decode_rw(raw.rw);
    auto kind = decode_kind(raw.kind);
    if (!master.has_value() || !rw.has_value() || !kind.has_value() || !(raw.size == 1 || raw.size == 2 || raw.size == 4)) {
      ++stats.malformed_lines;
      std::cerr << "warning: malformed binary record at index " << index << " skipped\n";
      ++index;
      continue;
    }

    TraceRecord rec{};
    rec.seq = raw.seq;
    rec.master = *master;
    rec.tick_first_attempt = raw.tick_first_attempt;
    rec.tick_complete = raw.tick_complete;
    rec.addr = raw.addr;
    rec.addr_text = to_hex32(raw.addr);
    rec.size = raw.size;
    rec.rw = *rw;
    rec.kind = *kind;
    rec.service_cycles = static_cast<std::uint32_t>(raw.service_cycles);
    rec.retries = static_cast<std::uint32_t>(raw.retries);
    rec.source_line = index + 1;

    if (seen_seq_values.find(rec.seq) != seen_seq_values.end()) {
      ++stats.duplicate_seq_count;
      std::cerr << "warning: duplicate seq " << rec.seq << " in binary record " << index << '\n';
    }
    seen_seq_values.insert(rec.seq);
    if (previous_seq.has_value() && rec.seq <= *previous_seq) {
      ++stats.non_monotonic_seq_count;
      std::cerr << "warning: non-monotonic seq " << rec.seq << " in binary record " << index << '\n';
    }
    previous_seq = rec.seq;
    records.push_back(std::move(rec));
    ++index;
  }
  return true;
}

bool load_input_records(const std::string &path, std::vector<TraceRecord> &records, InputStats &stats) {
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".bin") {
    return load_binary_records(path, records, stats);
  }
  return load_jsonl_records(path, records, stats);
}

bool parse_options(int argc, char **argv, Options &opts) {
  if (argc < 2) {
    return false;
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_help();
      std::exit(0);
    }
    if (arg == "--annotated-output") {
      if (i + 1 >= argc) return false;
      opts.annotated_output_path = std::string(argv[++i]);
      continue;
    }
    if (arg == "--summary-output") {
      if (i + 1 >= argc) return false;
      opts.summary_output_path = std::string(argv[++i]);
      continue;
    }
    if (arg == "--summary-only") {
      opts.summary_only = true;
      continue;
    }
    if (arg == "--include-model-comparison") {
      opts.include_model_comparison = true;
      continue;
    }
    if (arg == "--annotated-limit") {
      if (i + 1 >= argc) return false;
      const auto parsed = parse_u64(argv[++i]);
      if (!parsed) return false;
      opts.annotated_limit = static_cast<std::size_t>(*parsed);
      continue;
    }
    if (arg == "--top" || arg == "--top-k") {
      if (i + 1 >= argc) return false;
      const auto parsed = parse_u64(argv[++i]);
      if (!parsed) return false;
      opts.top_k = static_cast<std::size_t>(*parsed);
      continue;
    }
    if (!arg.empty() && arg[0] != '-') {
      opts.input_path = arg;
      continue;
    }
    return false;
  }
  return !opts.input_path.empty();
}

} // namespace

int main(int argc, char **argv) {
  Options options{};
  if (!parse_options(argc, argv, options)) {
    print_help();
    return 1;
  }

  std::vector<TraceRecord> records;
  InputStats input_stats{};
  if (!load_input_records(options.input_path, records, input_stats)) {
    return 1;
  }

  const std::size_t malformed_lines = input_stats.malformed_lines;
  const std::size_t duplicate_seq_count = input_stats.duplicate_seq_count;
  const std::size_t non_monotonic_seq_count = input_stats.non_monotonic_seq_count;
  const std::size_t nonempty_lines = input_stats.total_events;

  std::stable_sort(records.begin(), records.end(), [](const TraceRecord &a, const TraceRecord &b) {
    if (a.tick_complete != b.tick_complete) return a.tick_complete < b.tick_complete;
    return a.seq < b.seq;
  });


  std::vector<TraceRecord> filtered_records;
  filtered_records.reserve(records.size());
  std::map<std::string, std::size_t> included_master_distribution;
  std::map<std::string, std::size_t> included_region_distribution;
  std::map<std::string, std::size_t> included_size_distribution;
  std::map<std::string, std::size_t> included_rw_distribution;
  std::map<std::string, std::size_t> included_access_kind_distribution;
  std::map<std::string, std::size_t> included_master_region_distribution;
  std::map<std::string, std::size_t> included_cache_bucket_distribution;
  std::map<std::string, std::size_t> included_master_region_access_kind_cache_bucket_distribution;
  std::map<std::string, std::size_t> excluded_reason_counts;
  std::map<std::string, std::size_t> known_gap_bucket_counts;

  for (const auto &record : records) {
    if (!parse_master(record.master).has_value()) {
      ++excluded_reason_counts["invalid_master"];
      std::cerr << "warning: invalid master on line " << record.source_line << " skipped\n";
      continue;
    }
    filtered_records.push_back(record);
    included_master_distribution[record.master] += 1;
    included_region_distribution[region_name(record.addr)] += 1;
    included_size_distribution[size_label(record.size)] += 1;
    included_rw_distribution[record.rw] += 1;
    included_access_kind_distribution[record.kind] += 1;
    included_master_region_distribution[record.master + " | " + region_name(record.addr)] += 1;
    const std::string bucket = classify_cache_bucket(record.addr, record.service_cycles);
    included_cache_bucket_distribution[bucket] += 1;
    included_master_region_access_kind_cache_bucket_distribution[record.master + " | " + region_name(record.addr) + " | " + record.kind + " | " + bucket] += 1;
    if (record.size == 1U) {
      known_gap_bucket_counts["byte_access_wait_check_gap_candidate"] += 1;
    }
  }

  excluded_reason_counts["malformed_line"] += malformed_lines;
  const std::size_t total_events = nonempty_lines;
  const std::size_t included_events = filtered_records.size();
  const std::size_t excluded_events = total_events >= included_events ? (total_events - included_events) : 0;

  const busarb::ArbiterConfig arbiter_config{};
  std::optional<busarb::Arbiter> arbiter;
  if (options.include_model_comparison) {
    arbiter.emplace(busarb::TimingCallbacks{busarb::ymir_access_cycles, nullptr}, arbiter_config);
  }
  std::optional<TraceRecord> previous_record_for_normalized;

  std::vector<ReplayResult> results;
  results.reserve(records.size());

  std::size_t known_gap_count = 0;
  std::size_t known_gap_byte_access_count = 0;
  std::size_t cumulative_agreement_count = 0;
  std::size_t cumulative_mismatch_count = 0;
  std::size_t normalized_agreement_count = 0;
  std::size_t normalized_mismatch_count = 0;

  std::map<std::string, std::size_t> histogram;
  std::map<std::string, std::size_t> normalized_by_master;
  std::map<std::string, std::size_t> normalized_by_region;
  std::map<std::string, std::size_t> normalized_by_size;
  std::map<std::string, std::size_t> normalized_mismatch_by_master_region_access_kind;
  std::map<std::string, std::size_t> sample_size_by_master_region_access_kind;
  std::map<std::string, std::vector<std::int64_t>> normalized_delta_by_access_kind;
  std::map<std::string, ObservedBucketStats> observed_bucket_stats_by_master_region_access_kind_cache_bucket;

  std::vector<std::int64_t> normalized_wait_deltas;
  normalized_wait_deltas.reserve(records.size());

  for (const auto &record : filtered_records) {
    const auto master = parse_master(record.master);

    ReplayResult r{};
    r.record = record;
    r.observed_service_cycles = record.service_cycles;
    r.observed_retries = record.retries;

    if (record.tick_complete >= record.tick_first_attempt) {
      r.ymir_elapsed = static_cast<std::uint32_t>(record.tick_complete - record.tick_first_attempt);
      r.ymir_wait_metric_kind = "exact_tick_elapsed_exclusive";
    } else {
      r.ymir_elapsed = static_cast<std::uint32_t>(r.ymir_retries * r.ymir_service_cycles + r.ymir_service_cycles);
      r.ymir_wait_metric_kind = "proxy_retries_x_service";
    }
    r.ymir_wait = (r.ymir_elapsed > r.ymir_service_cycles) ? (r.ymir_elapsed - r.ymir_service_cycles) : 0U;
    r.cache_bucket = classify_cache_bucket(record.addr, r.ymir_service_cycles);

    const std::string observed_bucket_key = record.master + " | " + region_name(record.addr) + " | " + record.kind + " | " + r.cache_bucket;
    auto &observed_bucket_stats = observed_bucket_stats_by_master_region_access_kind_cache_bucket[observed_bucket_key];
    observed_bucket_stats.sample_size += 1;
    observed_bucket_stats.observed_wait_nonzero_count += (r.ymir_wait > 0U ? 1U : 0U);
    observed_bucket_stats.observed_elapsed_values.push_back(static_cast<std::int64_t>(r.ymir_elapsed));
    observed_bucket_stats.observed_wait_values.push_back(static_cast<std::int64_t>(r.ymir_wait));

    if (options.include_model_comparison) {
      const busarb::BusRequest req{*master, record.addr, record.rw == "W", record.size, record.tick_first_attempt};
      const std::uint64_t bus_before_commit = arbiter->bus_free_tick();
      arbiter->commit_grant(req, record.tick_first_attempt);
      const std::uint64_t bus_after_commit = arbiter->bus_free_tick();

      r.arbiter_predicted_wait = estimate_local_wait_cycles(record, previous_record_for_normalized, arbiter_config);
      r.arbiter_predicted_service = std::max(1U, busarb::ymir_access_cycles(nullptr, record.addr, record.rw == "W", record.size));
      r.arbiter_predicted_total = r.arbiter_predicted_wait + r.arbiter_predicted_service;
      r.base_latency = r.arbiter_predicted_service;
      r.contention_stall = r.arbiter_predicted_wait;
      r.total_predicted = r.arbiter_predicted_total;

      r.normalized_delta_wait = static_cast<std::int64_t>(r.arbiter_predicted_wait) - static_cast<std::int64_t>(r.ymir_wait);
      r.normalized_delta_total = static_cast<std::int64_t>(r.arbiter_predicted_total) - static_cast<std::int64_t>(r.ymir_elapsed);

      const std::uint64_t ymir_start = record.tick_first_attempt;
      const std::uint64_t ymir_end_exclusive = record.tick_complete + 1U;
      const std::uint64_t arbiter_start = std::max(bus_before_commit, ymir_start);
      r.cumulative_drift_wait = static_cast<std::int64_t>(arbiter_start) - static_cast<std::int64_t>(ymir_start);
      r.cumulative_drift_total = static_cast<std::int64_t>(bus_after_commit) - static_cast<std::int64_t>(ymir_end_exclusive);

      const bool known_byte_gap = (record.size == 1U && r.ymir_retries == 0U && r.normalized_delta_wait > 0);
      if (known_byte_gap) {
        r.classification = "known_ymir_wait_model_gap";
        r.known_gap_reason = "byte_access_wait_check_gap";
        ++known_gap_count;
        ++known_gap_byte_access_count;
      } else if (r.cumulative_drift_wait == 0 && r.cumulative_drift_total == 0) {
        r.classification = "agreement";
        ++cumulative_agreement_count;
      } else {
        r.classification = "mismatch";
        ++cumulative_mismatch_count;
      }

      if (r.normalized_delta_wait == 0) {
        ++normalized_agreement_count;
      } else {
        ++normalized_mismatch_count;
      }

      histogram[region_name(record.addr) + " | " + r.classification] += 1;
      normalized_by_master[record.master] += (r.normalized_delta_wait == 0 ? 0U : 1U);
      normalized_by_region[region_name(record.addr)] += (r.normalized_delta_wait == 0 ? 0U : 1U);
      normalized_by_size[std::to_string(record.size)] += (r.normalized_delta_wait == 0 ? 0U : 1U);
      const std::string mk = record.master + " | " + region_name(record.addr) + " | " + record.kind;
      sample_size_by_master_region_access_kind[mk] += 1;
      if (r.normalized_delta_wait != 0) {
        normalized_mismatch_by_master_region_access_kind[mk] += 1;
      }
      normalized_delta_by_access_kind[record.kind].push_back(r.normalized_delta_wait);
      normalized_wait_deltas.push_back(r.normalized_delta_wait);
    } else {
      r.classification = (r.ymir_wait > 0U) ? "wait_nonzero" : "wait_zero";
      histogram[region_name(record.addr) + " | " + r.classification] += 1;
      ++cumulative_agreement_count;
    }

    results.push_back(r);
    if (options.include_model_comparison) {
      previous_record_for_normalized = record;
    }
  }

  const std::size_t records_processed = results.size();

  if (cumulative_agreement_count + cumulative_mismatch_count + known_gap_count != records_processed) {
    std::cerr << "error: classification invariant failed\n";
    return 1;
  }
  if (options.include_model_comparison && normalized_agreement_count + normalized_mismatch_count != records_processed) {
    std::cerr << "error: normalized invariant failed\n";
    return 1;
  }

  std::vector<const ReplayResult *> top_cumulative;
  top_cumulative.reserve(results.size());
  std::vector<const ReplayResult *> top_normalized;
  top_normalized.reserve(results.size());
  for (const auto &r : results) {
    top_cumulative.push_back(&r);
    top_normalized.push_back(&r);
  }

  std::stable_sort(top_cumulative.begin(), top_cumulative.end(), [](const ReplayResult *a, const ReplayResult *b) {
    return std::llabs(a->cumulative_drift_total) > std::llabs(b->cumulative_drift_total);
  });
  std::stable_sort(top_normalized.begin(), top_normalized.end(), [](const ReplayResult *a, const ReplayResult *b) {
    return std::llabs(a->model_vs_trace_wait_delta) > std::llabs(b->model_vs_trace_wait_delta);
  });

  if (!options.summary_only && options.annotated_output_path.has_value()) {
    std::ofstream annotated(*options.annotated_output_path);
    if (!annotated.is_open()) {
      std::cerr << "Failed to open annotated output path: " << *options.annotated_output_path << '\n';
      return 1;
    }

    const std::size_t emit_limit = options.annotated_limit.has_value() ? std::min(*options.annotated_limit, results.size()) : results.size();
    for (std::size_t i = 0; i < emit_limit; ++i) {
      const auto &r = results[i];
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
                << "\"observed_service_cycles\":" << r.ymir_service_cycles << ','
                << "\"observed_retries\":" << r.ymir_retries << ','
                << "\"observed_elapsed\":" << r.ymir_elapsed << ','
                << "\"observed_wait\":" << r.ymir_wait << ','
                << "\"observed_wait_metric_kind\":\"" << r.ymir_wait_metric_kind << "\"," 
                << "\"cache_bucket\":\"" << r.cache_bucket << "\"," 
                << "\"classification\":\"" << r.classification << "\"";
      if (options.include_model_comparison) {
        annotated << ','
                  << "\"model_predicted_wait\":" << r.arbiter_predicted_wait << ','
                  << "\"model_predicted_service\":" << r.arbiter_predicted_service << ','
                  << "\"model_predicted_total\":" << r.arbiter_predicted_total << ','
                  << "\"model_vs_trace_wait_delta\":" << r.normalized_delta_wait << ','
                  << "\"model_vs_trace_total_delta\":" << r.normalized_delta_total << ','
                  << "\"cumulative_drift_wait\":" << r.cumulative_drift_wait << ','
                  << "\"cumulative_drift_total\":" << r.cumulative_drift_total << ','
                  << "\"known_gap_reason\":\"" << r.known_gap_reason << "\"";
      }
      annotated << "}\n";
    }
  }

  std::uint64_t sum_base_latency = 0;
  std::uint64_t sum_contention_stall = 0;
  std::uint64_t sum_total_predicted = 0;
  for (const auto &r : results) {
    sum_base_latency += r.base_latency;
    sum_contention_stall += r.contention_stall;
    sum_total_predicted += r.total_predicted;
  }

  const double mean_normalized_delta_wait = normalized_wait_deltas.empty()
                                                ? 0.0
                                                : static_cast<double>(std::accumulate(normalized_wait_deltas.begin(), normalized_wait_deltas.end(), std::int64_t{0})) /
                                                      static_cast<double>(normalized_wait_deltas.size());

  if (options.summary_output_path.has_value()) {
    std::ofstream summary(*options.summary_output_path);
    if (!summary.is_open()) {
      std::cerr << "Failed to open summary output path: " << *options.summary_output_path << '\n';
      return 1;
    }

    summary << "{\n";
    summary << "  \"summary_schema_version\": " << kSummarySchemaVersion << ",\n";
    summary << "  \"records_processed\": " << records_processed << ",\n";
    summary << "  \"malformed_lines_skipped\": " << malformed_lines << ",\n";
    summary << "  \"duplicate_seq_count\": " << duplicate_seq_count << ",\n";
    summary << "  \"non_monotonic_seq_count\": " << non_monotonic_seq_count << ",\n";
    summary << "  \"total_events\": " << total_events << ",\n";
    summary << "  \"included_events\": " << included_events << ",\n";
    summary << "  \"excluded_events\": " << excluded_events << ",\n";
    summary << "  \"trace_observed\": {\n";
    summary << "    \"source\": \"TRACE_ONLY\",\n";
    summary << "    \"records_processed\": " << records_processed << ",\n";
    summary << "    \"observed_wait_nonzero_count\": " << std::count_if(results.begin(), results.end(), [](const ReplayResult &r) { return r.ymir_wait > 0U; }) << "\n";
    summary << "  },\n";

    std::map<std::string, std::pair<std::optional<ObservedBucketStats>, std::optional<ObservedBucketStats>>> symmetry_candidates;
    for (const auto &[key, stats] : observed_bucket_stats_by_master_region_access_kind_cache_bucket) {
      const std::string delim = " | ";
      const auto p1 = key.find(delim);
      if (p1 == std::string::npos) continue;
      const auto p2 = key.find(delim, p1 + delim.size());
      if (p2 == std::string::npos) continue;
      const auto p3 = key.find(delim, p2 + delim.size());
      if (p3 == std::string::npos) continue;
      const std::string master = key.substr(0, p1);
      const std::string rest = key.substr(p1 + delim.size());
      if (master == "MSH2") {
        symmetry_candidates[rest].first = stats;
      } else if (master == "SSH2") {
        symmetry_candidates[rest].second = stats;
      }
    }
    std::vector<SymmetryCheckEntry> symmetry_checks;
    for (const auto &[bucket, pair_stats] : symmetry_candidates) {
      if (!pair_stats.first.has_value() || !pair_stats.second.has_value()) {
        continue;
      }
      const auto &msh2 = *pair_stats.first;
      const auto &ssh2 = *pair_stats.second;
      if (msh2.sample_size < 100U || ssh2.sample_size < 100U) {
        continue;
      }
      SymmetryCheckEntry entry{};
      entry.bucket = bucket;
      entry.msh2 = msh2;
      entry.ssh2 = ssh2;
      const double msh2_elapsed_p50 = percentile(msh2.observed_elapsed_values, 0.5);
      const double ssh2_elapsed_p50 = percentile(ssh2.observed_elapsed_values, 0.5);
      const double msh2_elapsed_p99 = percentile(msh2.observed_elapsed_values, 0.99);
      const double ssh2_elapsed_p99 = percentile(ssh2.observed_elapsed_values, 0.99);
      const double msh2_wait_rate = msh2.sample_size == 0 ? 0.0 : static_cast<double>(msh2.observed_wait_nonzero_count) / static_cast<double>(msh2.sample_size);
      const double ssh2_wait_rate = ssh2.sample_size == 0 ? 0.0 : static_cast<double>(ssh2.observed_wait_nonzero_count) / static_cast<double>(ssh2.sample_size);
      std::vector<std::string> divergence_notes;
      if (std::fabs(msh2_elapsed_p50 - ssh2_elapsed_p50) > 1.0) {
        divergence_notes.push_back("elapsed_p50");
      }
      if (std::fabs(msh2_elapsed_p99 - ssh2_elapsed_p99) > 2.0) {
        divergence_notes.push_back("elapsed_p99");
      }
      if (std::fabs(msh2_wait_rate - ssh2_wait_rate) > 0.01) {
        divergence_notes.push_back("wait_nonzero_rate");
      }
      entry.symmetric = divergence_notes.empty();
      if (!entry.symmetric) {
        for (std::size_t i = 0; i < divergence_notes.size(); ++i) {
          if (i > 0) entry.notes += ",";
          entry.notes += divergence_notes[i];
        }
      }
      symmetry_checks.push_back(std::move(entry));
    }

    auto write_map = [&summary](const std::string &name, const std::map<std::string, std::size_t> &m, bool trailing_comma) {
      summary << "  \"" << name << "\": {\n";
      std::size_t i = 0;
      for (const auto &[k, v] : m) {
        summary << "    \"" << json_escape(k) << "\": " << v;
        ++i;
        if (i < m.size()) summary << ',';
        summary << "\n";
      }
      summary << "  }";
      if (trailing_comma) summary << ',';
      summary << "\n";
    };

    if (options.include_model_comparison) {
      summary << "  \"agreement_count\": " << cumulative_agreement_count << ",\n";
      summary << "  \"mismatch_count\": " << cumulative_mismatch_count << ",\n";
      summary << "  \"known_gap_count\": " << known_gap_count << ",\n";
      summary << "  \"known_gap_byte_access_count\": " << known_gap_byte_access_count << ",\n";
      summary << "  \"normalized_agreement_count\": " << normalized_agreement_count << ",\n";
      summary << "  \"normalized_mismatch_count\": " << normalized_mismatch_count << ",\n";
      summary << "  \"mean_base_latency\": " << (records_processed == 0 ? 0.0 : static_cast<double>(sum_base_latency) / static_cast<double>(records_processed)) << ",\n";
      summary << "  \"mean_contention_stall\": " << (records_processed == 0 ? 0.0 : static_cast<double>(sum_contention_stall) / static_cast<double>(records_processed)) << ",\n";
      summary << "  \"mean_total_predicted\": " << (records_processed == 0 ? 0.0 : static_cast<double>(sum_total_predicted) / static_cast<double>(records_processed)) << ",\n";
      summary << "  \"mean_normalized_delta_wait\": " << mean_normalized_delta_wait << ",\n";
      summary << "  \"median_normalized_delta_wait\": " << percentile(normalized_wait_deltas, 0.5) << ",\n";
      summary << "  \"max_normalized_delta_wait\": " << (normalized_wait_deltas.empty() ? 0.0 : percentile(normalized_wait_deltas, 1.0)) << ",\n";
      summary << "  \"p90_normalized_delta_wait\": " << percentile(normalized_wait_deltas, 0.9) << ",\n";
      summary << "  \"p99_normalized_delta_wait\": " << percentile(normalized_wait_deltas, 0.99) << ",\n";
      summary << "  \"final_cumulative_drift_wait\": " << (results.empty() ? 0 : results.back().cumulative_drift_wait) << ",\n";
      summary << "  \"final_cumulative_drift_total\": " << (results.empty() ? 0 : results.back().cumulative_drift_total) << ",\n";
      summary << "  \"drift_rate_wait_per_record\": " << (results.empty() ? 0.0 : static_cast<double>(results.back().cumulative_drift_wait) / static_cast<double>(results.size()))
              << ",\n";
      summary << "  \"drift_rate_total_per_record\": "
              << (results.empty() ? 0.0 : static_cast<double>(results.back().cumulative_drift_total) / static_cast<double>(results.size())) << ",\n";

    }

    write_map("excluded_reason_counts", excluded_reason_counts, true);
    write_map("known_gap_bucket_counts", known_gap_bucket_counts, true);
    write_map("included_master_distribution", included_master_distribution, true);
    write_map("included_region_distribution", included_region_distribution, true);
    write_map("included_size_distribution", included_size_distribution, true);
    write_map("included_rw_distribution", included_rw_distribution, true);
    write_map("included_access_kind_distribution", included_access_kind_distribution, true);
    write_map("included_master_region_distribution", included_master_region_distribution, true);
    write_map("cache_bucket_distribution", included_cache_bucket_distribution, true);
    write_map("master_region_access_kind_cache_bucket_distribution", included_master_region_access_kind_cache_bucket_distribution, true);

    summary << "  \"observed_bucket_stats_by_master_region_access_kind_cache_bucket\": {\n";
    std::size_t observed_bucket_index = 0;
    for (const auto &[key, stats] : observed_bucket_stats_by_master_region_access_kind_cache_bucket) {
      const double wait_nonzero_rate = stats.sample_size == 0 ? 0.0 : static_cast<double>(stats.observed_wait_nonzero_count) / static_cast<double>(stats.sample_size);
      const bool low_sample = stats.sample_size < 100U;
      summary << "    \"" << json_escape(key) << "\": {"
              << "\"sample_size\": " << stats.sample_size
              << ", \"observed_wait_nonzero_count\": " << stats.observed_wait_nonzero_count
              << ", \"observed_wait_nonzero_rate\": " << wait_nonzero_rate
              << ", \"observed_elapsed_p50\": " << percentile(stats.observed_elapsed_values, 0.5)
              << ", \"observed_elapsed_p90\": " << percentile(stats.observed_elapsed_values, 0.9)
              << ", \"observed_elapsed_p99\": " << percentile(stats.observed_elapsed_values, 0.99)
              << ", \"observed_wait_p50\": " << percentile(stats.observed_wait_values, 0.5)
              << ", \"observed_wait_p90\": " << percentile(stats.observed_wait_values, 0.9)
              << ", \"observed_wait_p99\": " << percentile(stats.observed_wait_values, 0.99)
              << ", \"low_sample\": " << (low_sample ? "true" : "false")
              << "}";
      ++observed_bucket_index;
      if (observed_bucket_index < observed_bucket_stats_by_master_region_access_kind_cache_bucket.size()) summary << ',';
      summary << "\n";
    }
    summary << "  },\n";

    summary << "  \"symmetry_checks\": [\n";
    for (std::size_t i = 0; i < symmetry_checks.size(); ++i) {
      const auto &entry = symmetry_checks[i];
      const double msh2_wait_rate = entry.msh2.sample_size == 0 ? 0.0 : static_cast<double>(entry.msh2.observed_wait_nonzero_count) / static_cast<double>(entry.msh2.sample_size);
      const double ssh2_wait_rate = entry.ssh2.sample_size == 0 ? 0.0 : static_cast<double>(entry.ssh2.observed_wait_nonzero_count) / static_cast<double>(entry.ssh2.sample_size);
      summary << "    {\"bucket\": \"" << json_escape(entry.bucket)
              << "\", \"msh2\": {\"N\": " << entry.msh2.sample_size
              << ", \"elapsed_p50\": " << percentile(entry.msh2.observed_elapsed_values, 0.5)
              << ", \"elapsed_p90\": " << percentile(entry.msh2.observed_elapsed_values, 0.9)
              << ", \"elapsed_p99\": " << percentile(entry.msh2.observed_elapsed_values, 0.99)
              << ", \"wait_nonzero_rate\": " << msh2_wait_rate
              << "}, \"ssh2\": {\"N\": " << entry.ssh2.sample_size
              << ", \"elapsed_p50\": " << percentile(entry.ssh2.observed_elapsed_values, 0.5)
              << ", \"elapsed_p90\": " << percentile(entry.ssh2.observed_elapsed_values, 0.9)
              << ", \"elapsed_p99\": " << percentile(entry.ssh2.observed_elapsed_values, 0.99)
              << ", \"wait_nonzero_rate\": " << ssh2_wait_rate
              << "}, \"symmetric\": " << (entry.symmetric ? "true" : "false")
              << ", \"notes\": \"" << json_escape(entry.notes) << "\"}";
      if (i + 1 < symmetry_checks.size()) summary << ',';
      summary << "\n";
    }
    summary << "  ],\n";

    summary << "  \"delta_histogram\": {\n";
    std::size_t hist_index = 0;
    for (const auto &[key, count] : histogram) {
      summary << "    \"" << json_escape(key) << "\": " << count;
      ++hist_index;
      if (hist_index < histogram.size()) summary << ',';
      summary << "\n";
    }
    summary << "  }";

    if (options.include_model_comparison) {
      summary << ",\n";
      summary << "  \"model_comparison\": {\n";
      summary << "    \"source\": \"MODEL_COMPARISON\",\n";
      summary << "    \"hypothesis_agreement_count\": " << normalized_agreement_count << ",\n";
      summary << "    \"hypothesis_mismatch_count\": " << normalized_mismatch_count << ",\n";
      summary << "    \"known_gap_count\": " << known_gap_count << ",\n";
      summary << "    \"known_gap_byte_access_count\": " << known_gap_byte_access_count << ",\n";
      summary << "    \"mean_model_predicted_service\": " << (records_processed == 0 ? 0.0 : static_cast<double>(sum_base_latency) / static_cast<double>(records_processed)) << ",\n";
      summary << "    \"mean_model_predicted_wait\": " << (records_processed == 0 ? 0.0 : static_cast<double>(sum_contention_stall) / static_cast<double>(records_processed)) << ",\n";
      summary << "    \"mean_model_predicted_total\": " << (records_processed == 0 ? 0.0 : static_cast<double>(sum_total_predicted) / static_cast<double>(records_processed)) << ",\n";
      summary << "    \"mean_model_vs_trace_wait_delta\": " << mean_normalized_delta_wait << ",\n";
      summary << "    \"median_model_vs_trace_wait_delta\": " << percentile(normalized_wait_deltas, 0.5) << ",\n";
      summary << "    \"max_model_vs_trace_wait_delta\": " << (normalized_wait_deltas.empty() ? 0.0 : percentile(normalized_wait_deltas, 1.0)) << ",\n";
      summary << "    \"p90_model_vs_trace_wait_delta\": " << percentile(normalized_wait_deltas, 0.9) << ",\n";
      summary << "    \"p99_model_vs_trace_wait_delta\": " << percentile(normalized_wait_deltas, 0.99) << ",\n";
      summary << "    \"final_cumulative_drift_wait\": " << (results.empty() ? 0 : results.back().cumulative_drift_wait) << ",\n";
      summary << "    \"final_cumulative_drift_total\": " << (results.empty() ? 0 : results.back().cumulative_drift_total) << ",\n";
      summary << "    \"drift_rate_wait_per_record\": "
              << (results.empty() ? 0.0 : static_cast<double>(results.back().cumulative_drift_wait) / static_cast<double>(results.size())) << ",\n";
      summary << "    \"drift_rate_total_per_record\": "
              << (results.empty() ? 0.0 : static_cast<double>(results.back().cumulative_drift_total) / static_cast<double>(results.size())) << ",\n";

      summary << "    \"hypothesis_mismatch_by_master_region_access_kind\": {\n";
      std::size_t mk_index = 0;
      for (const auto &[key, sample_size] : sample_size_by_master_region_access_kind) {
        const std::size_t mismatch_count = normalized_mismatch_by_master_region_access_kind.count(key) ? normalized_mismatch_by_master_region_access_kind.at(key) : 0U;
        const double mismatch_rate = sample_size == 0 ? 0.0 : static_cast<double>(mismatch_count) / static_cast<double>(sample_size);
        summary << "      \"" << json_escape(key) << "\": {\"mismatch_count\": " << mismatch_count
                << ", \"sample_size\": " << sample_size
                << ", \"mismatch_rate\": " << mismatch_rate << "}";
        ++mk_index;
        if (mk_index < sample_size_by_master_region_access_kind.size()) summary << ',';
        summary << "\n";
      }
      summary << "    },\n";

      summary << "    \"model_vs_trace_wait_delta_by_access_kind\": {\n";
      std::size_t kind_index = 0;
      for (const auto &[kind, deltas] : normalized_delta_by_access_kind) {
        summary << "      \"" << json_escape(kind) << "\": {\"sample_size\": " << deltas.size()
                << ", \"p90\": " << percentile(deltas, 0.9)
                << ", \"p99\": " << percentile(deltas, 0.99) << "}";
        ++kind_index;
        if (kind_index < normalized_delta_by_access_kind.size()) summary << ',';
        summary << "\n";
      }
      summary << "    },\n";

      summary << "    \"hypothesis_mismatch_by_master\": {\n";
      std::size_t by_master_idx = 0;
      for (const auto &[k, v] : normalized_by_master) {
        summary << "      \"" << json_escape(k) << "\": " << v;
        if (++by_master_idx < normalized_by_master.size()) summary << ',';
        summary << "\n";
      }
      summary << "    },\n";

      summary << "    \"hypothesis_mismatch_by_region\": {\n";
      std::size_t by_region_idx = 0;
      for (const auto &[k, v] : normalized_by_region) {
        summary << "      \"" << json_escape(k) << "\": " << v;
        if (++by_region_idx < normalized_by_region.size()) summary << ',';
        summary << "\n";
      }
      summary << "    },\n";

      summary << "    \"hypothesis_mismatch_by_size\": {\n";
      std::size_t by_size_idx = 0;
      for (const auto &[k, v] : normalized_by_size) {
        summary << "      \"" << json_escape(k) << "\": " << v;
        if (++by_size_idx < normalized_by_size.size()) summary << ',';
        summary << "\n";
      }
      summary << "    },\n";

      summary << "    \"top_cumulative_drifts\": [\n";
      const std::size_t emit = std::min(options.top_k, top_cumulative.size());
      for (std::size_t i = 0; i < emit; ++i) {
        const auto *r = top_cumulative[i];
        summary << "      {\"rank\": " << (i + 1) << ", \"seq\": " << r->record.seq << ", \"master\": \"" << json_escape(r->record.master)
                << "\", \"addr\": \"" << json_escape(r->record.addr_text) << "\", \"size\": " << static_cast<unsigned>(r->record.size)
                << ", \"cumulative_drift_wait\": " << r->cumulative_drift_wait << ", \"cumulative_drift_total\": " << r->cumulative_drift_total
                << ", \"model_vs_trace_wait_delta\": " << r->normalized_delta_wait << ", \"model_vs_trace_total_delta\": " << r->normalized_delta_total
                << ", \"classification\": \"" << json_escape(r->classification) << "\", \"region\": \"" << json_escape(region_name(r->record.addr))
                << "\"}";
        if (i + 1 < emit) summary << ',';
        summary << '\n';
      }
      summary << "    ],\n";

      summary << "    \"top_model_vs_trace_wait_deltas\": [\n";
      const std::size_t emit_norm = std::min(options.top_k, top_normalized.size());
      for (std::size_t i = 0; i < emit_norm; ++i) {
        const auto *r = top_normalized[i];
        summary << "      {\"rank\": " << (i + 1) << ", \"seq\": " << r->record.seq << ", \"master\": \"" << json_escape(r->record.master)
                << "\", \"addr\": \"" << json_escape(r->record.addr_text) << "\", \"size\": " << static_cast<unsigned>(r->record.size)
                << ", \"model_vs_trace_wait_delta\": " << r->normalized_delta_wait << ", \"model_vs_trace_total_delta\": " << r->normalized_delta_total
                << ", \"cumulative_drift_wait\": " << r->cumulative_drift_wait << ", \"cumulative_drift_total\": " << r->cumulative_drift_total
                << ", \"classification\": \"" << json_escape(r->classification) << "\", \"region\": \"" << json_escape(region_name(r->record.addr))
                << "\"}";
        if (i + 1 < emit_norm) summary << ',';
        summary << '\n';
      }
      summary << "    ]\n";
      summary << "  }\n";
    } else {
      summary << "\n";
    }

    summary << "}\n";
  }

  std::cout << "dataset_hygiene_summary:\n";
  std::cout << "  total_events: " << total_events << "\n";
  std::cout << "  included_events: " << included_events << "\n";
  std::cout << "  excluded_events: " << excluded_events << "\n";
  std::cout << "  excluded_malformed_line: " << excluded_reason_counts["malformed_line"] << "\n";
  std::cout << "  excluded_invalid_master: " << excluded_reason_counts["invalid_master"] << "\n";
  std::cout << "  known_gap_bucket_byte_access_wait_check_gap_candidate: "
            << known_gap_bucket_counts["byte_access_wait_check_gap_candidate"] << "\n";
  std::cout << "  master_distribution:\n";
  for (const auto &[key, count] : included_master_distribution) {
    std::cout << "    " << key << " => " << count << "\n";
  }
  std::cout << "  region_distribution:\n";
  for (const auto &[key, count] : included_region_distribution) {
    std::cout << "    " << key << " => " << count << "\n";
  }
  std::cout << "  size_distribution:\n";
  for (const auto &[key, count] : included_size_distribution) {
    std::cout << "    " << key << " => " << count << "\n";
  }
  std::cout << "  rw_distribution:\n";
  for (const auto &[key, count] : included_rw_distribution) {
    std::cout << "    " << key << " => " << count << "\n";
  }
  std::cout << "  access_kind_distribution:\n";
  for (const auto &[key, count] : included_access_kind_distribution) {
    std::cout << "    " << key << " => " << count << "\n";
  }
  std::cout << "  cache_bucket_distribution:\n";
  for (const auto &[key, count] : included_cache_bucket_distribution) {
    std::cout << "    " << key << " => " << count << "\n";
  }

  std::cout << "records_processed: " << records_processed << "\n";
  std::cout << "malformed_lines_skipped: " << malformed_lines << "\n";
  std::cout << "duplicate_seq_count: " << duplicate_seq_count << "\n";
  std::cout << "non_monotonic_seq_count: " << non_monotonic_seq_count << "\n";
  if (options.include_model_comparison) {
    std::cout << "Model comparison: ENABLED (hypothesis mode)\n";
    std::cout << "agreement_count: " << cumulative_agreement_count << "\n";
    std::cout << "mismatch_count: " << cumulative_mismatch_count << "\n";
    std::cout << "known_gap_count: " << known_gap_count << "\n";
    std::cout << "normalized_agreement_count: " << normalized_agreement_count << "\n";
    std::cout << "normalized_mismatch_count: " << normalized_mismatch_count << "\n";
    std::cout << "final_cumulative_drift_total: " << (results.empty() ? 0 : results.back().cumulative_drift_total) << "\n";
  } else {
    std::cout << "Model comparison: DISABLED (trace-only mode)\n";
  }
  std::cout << "delta_histogram:\n";
  for (const auto &[key, count] : histogram) {
    std::cout << "  " << key << " => " << count << "\n";
  }

  if (options.include_model_comparison) {
    std::cout << "top_cumulative_drifts:\n";
    for (std::size_t i = 0; i < std::min(options.top_k, top_cumulative.size()); ++i) {
      const auto *r = top_cumulative[i];
      std::cout << "  #" << (i + 1) << " seq=" << r->record.seq << " cumulative_drift_total=" << r->cumulative_drift_total
                << " normalized_delta_wait=" << r->normalized_delta_wait << " class=" << r->classification << "\n";
    }

    std::cout << "top_normalized_deltas:\n";
    for (std::size_t i = 0; i < std::min(options.top_k, top_normalized.size()); ++i) {
      const auto *r = top_normalized[i];
      std::cout << "  #" << (i + 1) << " seq=" << r->record.seq << " normalized_delta_wait=" << r->normalized_delta_wait
                << " cumulative_drift_total=" << r->cumulative_drift_total << " class=" << r->classification << "\n";
    }
  }

  return 0;
}
