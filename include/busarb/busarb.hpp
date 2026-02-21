#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace busarb {

enum class BusMasterId : std::uint8_t {
  SH2_A = 0,
  SH2_B = 1,
  DMA = 2,
};

struct TimingCallbacks {
  std::uint32_t (*access_cycles)(void *ctx, std::uint32_t addr, bool is_write, std::uint8_t size_bytes) = nullptr;
  void *ctx = nullptr;
};

struct BusRequest {
  BusMasterId master_id = BusMasterId::SH2_A;
  std::uint32_t addr = 0;
  bool is_write = false;
  std::uint8_t size_bytes = 4;
  std::uint64_t now_tick = 0;
};

struct BusWaitResult {
  bool should_wait = false;
  std::uint32_t wait_cycles = 0;
};

class Arbiter {
public:
  explicit Arbiter(TimingCallbacks callbacks);

  [[nodiscard]] BusWaitResult query_wait(const BusRequest &req) const;
  void commit_grant(const BusRequest &req, std::uint64_t tick_start);

  [[nodiscard]] std::optional<std::size_t> pick_winner(const std::vector<BusRequest> &same_tick_requests) const;
  [[nodiscard]] std::uint64_t bus_free_tick() const;

private:
  [[nodiscard]] std::uint32_t service_cycles(const BusRequest &req) const;
  [[nodiscard]] static int priority(BusMasterId id);

  TimingCallbacks callbacks_{};
  std::uint64_t bus_free_tick_ = 0;
};

} // namespace busarb
