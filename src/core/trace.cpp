#include "core/trace.hpp"

#include <sstream>

namespace saturnis::core {

void TraceLog::add_commit(const CommitEvent &event) {
  std::ostringstream ss;
  ss << "COMMIT "
     << "{\"t_start\":" << event.t_start << ",\"t_end\":" << event.t_end
     << ",\"stall\":" << event.stall << ",\"cpu\":" << event.op.cpu_id
     << ",\"kind\":\"" << bus::kind_name(event.op.kind) << "\",\"phys\":" << event.op.phys_addr
     << ",\"size\":" << static_cast<unsigned>(event.op.size) << ",\"val\":" << event.value
     << ",\"src\":\"" << bus::source_name(event.op)
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

std::string TraceLog::to_jsonl() const {
  std::ostringstream ss;
  write_jsonl(ss);
  return ss.str();
}

void TraceLog::write_jsonl(std::ostream &os) const {
  for (const auto &line : lines_) {
    os << line << '\n';
  }
}

} // namespace saturnis::core
