#include "core/trace.hpp"

#include <locale>
#include <sstream>

namespace saturnis::core {
namespace {
constexpr std::uint32_t kTraceVersion = 1U;
}

void TraceLog::set_halt_on_fault(bool enabled) {
  halt_on_fault_ = enabled;
  if (!enabled) {
    should_halt_ = false;
  }
}

bool TraceLog::halt_on_fault() const { return halt_on_fault_; }

bool TraceLog::should_halt() const { return should_halt_; }

void TraceLog::add_commit(const CommitEvent &event) {
  std::ostringstream ss;
  ss << "COMMIT "
     << "{\"t_start\":" << event.t_start << ",\"t_end\":" << event.t_end
     << ",\"stall\":" << event.stall << ",\"cpu\":" << event.op.cpu_id
     << ",\"kind\":\"" << bus::kind_name(event.op.kind) << "\",\"phys\":" << event.op.phys_addr
     << ",\"size\":" << static_cast<unsigned>(event.op.size) << ",\"val\":" << event.value
     << ",\"src\":\"" << bus::source_name(event.op)
     << "\",\"owner\":\"" << bus::owner_name(event.op)
     << "\",\"tag\":\"" << bus::provenance_tag(event.op)
     << "\",\"cache_hit\":" << (event.cache_hit ? "true" : "false")
     << "}";
  lines_.push_back(ss.str());
}

void TraceLog::add_state(const CpuSnapshot &state) {
  std::ostringstream ss;
  ss << "STATE "
     << "{\"t\":" << state.t << ",\"cpu\":" << state.cpu << ",\"pc\":" << state.pc
     << ",\"sr\":" << state.sr << ",\"r\":[";
  for (std::size_t i = 0; i < state.r.size(); ++i) {
    if (i != 0U) {
      ss << ',';
    }
    ss << state.r[i];
  }
  ss << "]}";
  lines_.push_back(ss.str());
}


void TraceLog::add_fault(const FaultEvent &fault) {
  std::ostringstream ss;
  ss << "FAULT "
     << "{\"t\":" << fault.t << ",\"cpu\":" << fault.cpu
     << ",\"pc\":" << fault.pc << ",\"detail\":" << fault.detail
     << ",\"reason\":\"" << fault.reason << "\"}";
  lines_.push_back(ss.str());
  if (halt_on_fault_) {
    should_halt_ = true;
  }
}

std::string TraceLog::to_jsonl() const {
  std::ostringstream ss;
  ss.imbue(std::locale::classic());
  write_jsonl(ss);
  return ss.str();
}

void TraceLog::write_jsonl(std::ostream &os) const {
  os << "TRACE {\"version\":" << kTraceVersion << "}\n";
  for (const auto &line : lines_) {
    os << line << '\n';
  }
}

} // namespace saturnis::core
