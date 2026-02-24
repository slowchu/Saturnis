#include "busarb/busarb.hpp"
#include "busarb/ymir_timing.hpp"

#include <algorithm>
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

inline constexpr std::uint32_t kSummarySchemaVersion = 3;

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

  std::uint32_t arbiter_predicted_service = 0;
  std::uint32_t arbiter_predicted_wait = 0;
  std::uint32_t arbiter_predicted_total = 0;

  std::uint32_t base_latency = 0;
  std::uint32_t contention_stall = 0;
  std::uint32_t total_predicted = 0;

  std::int64_t normalized_delta_wait = 0;
  std::int64_t normalized_delta_total = 0;

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
  std::optional<std::size_t> annotated_limit;
};

void print_help() {
  std::cout << "Usage: trace_replay <input.jsonl> [options]\n"
            << "  --annotated-output <path>   Write annotated JSONL\n"
            << "  --summary-output <path>     Write machine-readable summary JSON\n"
            << "  --summary-only              Skip annotated output even if path supplied\n"
            << "  --annotated-limit <N>       Emit first N annotated rows\n"
            << "  --top <N>                   Legacy alias for --top-k\n"
            << "  --top-k <N>                 Number of ranked entries to emit\n"
            << "  --help                      Show this help\n"
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

  std::ifstream input(options.input_path);
  if (!input.is_open()) {
    std::cerr << "Failed to open input file: " << options.input_path << '\n';
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
  std::size_t nonempty_lines = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    ++nonempty_lines;
    auto rec = parse_record(line, line_number);
    if (!rec.has_value()) {
      ++malformed_lines;
      std::cerr << "warning: malformed line " << line_number << " skipped\n";
      continue;
    }
    if (seen_seq_values.find(rec->seq) != seen_seq_values.end()) {
      ++duplicate_seq_count;
      std::cerr << "warning: duplicate seq " << rec->seq << " on line " << line_number << '\n';
    }
    seen_seq_values.insert(rec->seq);

    if (previous_seq_in_input.has_value() && rec->seq <= *previous_seq_in_input) {
      ++non_monotonic_seq_count;
      std::cerr << "warning: non-monotonic seq " << rec->seq << " on line " << line_number << '\n';
    }
    previous_seq_in_input = rec->seq;
    records.push_back(*rec);
  }

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
    if (record.size == 1U) {
      known_gap_bucket_counts["byte_access_wait_check_gap_candidate"] += 1;
    }
  }

  excluded_reason_counts["malformed_line"] += malformed_lines;
  const std::size_t total_events = nonempty_lines;
  const std::size_t included_events = filtered_records.size();
  const std::size_t excluded_events = total_events >= included_events ? (total_events - included_events) : 0;

  const busarb::ArbiterConfig arbiter_config{};
  busarb::Arbiter arbiter({busarb::ymir_access_cycles, nullptr}, arbiter_config);
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

  std::vector<std::int64_t> normalized_wait_deltas;
  normalized_wait_deltas.reserve(records.size());

  for (const auto &record : records) {
    const auto master = parse_master(record.master);
    if (!master.has_value()) {
      ++malformed_lines;
      std::cerr << "warning: invalid master on line " << record.source_line << " skipped\n";
      continue;
    }

    const busarb::BusRequest req{*master, record.addr, record.rw == "W", record.size, record.tick_first_attempt};
    const std::uint64_t bus_before_commit = arbiter.bus_free_tick();
    arbiter.commit_grant(req, record.tick_first_attempt);
    const std::uint64_t bus_after_commit = arbiter.bus_free_tick();

    ReplayResult r{};
    r.record = record;
    r.ymir_service_cycles = record.service_cycles;
    r.ymir_retries = record.retries;

    if (record.tick_complete >= record.tick_first_attempt) {
      r.ymir_elapsed = static_cast<std::uint32_t>(record.tick_complete - record.tick_first_attempt);
      r.ymir_wait_metric_kind = "exact_tick_elapsed_exclusive";
    } else {
      r.ymir_elapsed = static_cast<std::uint32_t>(r.ymir_retries * r.ymir_service_cycles + r.ymir_service_cycles);
      r.ymir_wait_metric_kind = "proxy_retries_x_service";
    }
    r.ymir_wait = (r.ymir_elapsed > r.ymir_service_cycles) ? (r.ymir_elapsed - r.ymir_service_cycles) : 0U;

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
    normalized_wait_deltas.push_back(r.normalized_delta_wait);

    results.push_back(r);
    previous_record_for_normalized = record;
  }

  const std::size_t records_processed = results.size();

  if (cumulative_agreement_count + cumulative_mismatch_count + known_gap_count != records_processed) {
    std::cerr << "error: classification invariant failed\n";
    return 1;
  }
  if (normalized_agreement_count + normalized_mismatch_count != records_processed) {
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
    return std::llabs(a->normalized_delta_wait) > std::llabs(b->normalized_delta_wait);
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
                << "\"ymir_service_cycles\":" << r.ymir_service_cycles << ','
                << "\"ymir_retries\":" << r.ymir_retries << ','
                << "\"ymir_elapsed\":" << r.ymir_elapsed << ','
                << "\"ymir_wait\":" << r.ymir_wait << ','
                << "\"ymir_wait_metric_kind\":\"" << r.ymir_wait_metric_kind << "\","
                << "\"arbiter_predicted_wait\":" << r.arbiter_predicted_wait << ','
                << "\"arbiter_predicted_service\":" << r.arbiter_predicted_service << ','
                << "\"arbiter_predicted_total\":" << r.arbiter_predicted_total << ','
                << "\"base_latency\":" << r.base_latency << ','
                << "\"contention_stall\":" << r.contention_stall << ','
                << "\"total_predicted\":" << r.total_predicted << ','
                << "\"normalized_delta_wait\":" << r.normalized_delta_wait << ','
                << "\"normalized_delta_total\":" << r.normalized_delta_total << ','
                << "\"cumulative_drift_wait\":" << r.cumulative_drift_wait << ','
                << "\"cumulative_drift_total\":" << r.cumulative_drift_total << ','
                << "\"classification\":\"" << r.classification << "\","
                << "\"known_gap_reason\":\"" << r.known_gap_reason << "\""
                << "}\n";
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

    write_map("excluded_reason_counts", excluded_reason_counts, true);
    write_map("known_gap_bucket_counts", known_gap_bucket_counts, true);
    write_map("included_master_distribution", included_master_distribution, true);
    write_map("included_region_distribution", included_region_distribution, true);
    write_map("included_size_distribution", included_size_distribution, true);
    write_map("included_rw_distribution", included_rw_distribution, true);

    summary << "  \"delta_histogram\": {\n";
    std::size_t hist_index = 0;
    for (const auto &[key, count] : histogram) {
      summary << "    \"" << json_escape(key) << "\": " << count;
      ++hist_index;
      if (hist_index < histogram.size()) summary << ',';
      summary << "\n";
    }
    summary << "  },\n";

    write_map("normalized_mismatch_by_master", normalized_by_master, true);
    write_map("normalized_mismatch_by_region", normalized_by_region, true);
    write_map("normalized_mismatch_by_size", normalized_by_size, true);

    summary << "  \"top_cumulative_drifts\": [\n";
    const std::size_t emit = std::min(options.top_k, top_cumulative.size());
    for (std::size_t i = 0; i < emit; ++i) {
      const auto *r = top_cumulative[i];
      summary << "    {\"rank\": " << (i + 1) << ", \"seq\": " << r->record.seq << ", \"master\": \"" << json_escape(r->record.master)
              << "\", \"addr\": \"" << json_escape(r->record.addr_text) << "\", \"size\": " << static_cast<unsigned>(r->record.size)
              << ", \"cumulative_drift_wait\": " << r->cumulative_drift_wait << ", \"cumulative_drift_total\": " << r->cumulative_drift_total
              << ", \"normalized_delta_wait\": " << r->normalized_delta_wait << ", \"normalized_delta_total\": " << r->normalized_delta_total
              << ", \"classification\": \"" << json_escape(r->classification) << "\", \"region\": \"" << json_escape(region_name(r->record.addr))
              << "\"}";
      if (i + 1 < emit) summary << ',';
      summary << '\n';
    }
    summary << "  ],\n";

    summary << "  \"top_normalized_deltas\": [\n";
    const std::size_t emit_norm = std::min(options.top_k, top_normalized.size());
    for (std::size_t i = 0; i < emit_norm; ++i) {
      const auto *r = top_normalized[i];
      summary << "    {\"rank\": " << (i + 1) << ", \"seq\": " << r->record.seq << ", \"master\": \"" << json_escape(r->record.master)
              << "\", \"addr\": \"" << json_escape(r->record.addr_text) << "\", \"size\": " << static_cast<unsigned>(r->record.size)
              << ", \"normalized_delta_wait\": " << r->normalized_delta_wait << ", \"normalized_delta_total\": " << r->normalized_delta_total
              << ", \"cumulative_drift_wait\": " << r->cumulative_drift_wait << ", \"cumulative_drift_total\": " << r->cumulative_drift_total
              << ", \"classification\": \"" << json_escape(r->classification) << "\", \"region\": \"" << json_escape(region_name(r->record.addr))
              << "\"}";
      if (i + 1 < emit_norm) summary << ',';
      summary << '\n';
    }
    summary << "  ]\n";

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

  std::cout << "records_processed: " << records_processed << "\n";
  std::cout << "malformed_lines_skipped: " << malformed_lines << "\n";
  std::cout << "duplicate_seq_count: " << duplicate_seq_count << "\n";
  std::cout << "non_monotonic_seq_count: " << non_monotonic_seq_count << "\n";
  std::cout << "agreement_count: " << cumulative_agreement_count << "\n";
  std::cout << "mismatch_count: " << cumulative_mismatch_count << "\n";
  std::cout << "known_gap_count: " << known_gap_count << "\n";
  std::cout << "normalized_agreement_count: " << normalized_agreement_count << "\n";
  std::cout << "normalized_mismatch_count: " << normalized_mismatch_count << "\n";
  std::cout << "final_cumulative_drift_total: " << (results.empty() ? 0 : results.back().cumulative_drift_total) << "\n";
  std::cout << "delta_histogram:\n";
  for (const auto &[key, count] : histogram) {
    std::cout << "  " << key << " => " << count << "\n";
  }

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

  return 0;
}
