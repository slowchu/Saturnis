#include "busarb/busarb.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void check(bool cond, const char *msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    std::exit(1);
  }
}

std::uint32_t fixed_cycles(void *, std::uint32_t, bool, std::uint8_t size) {
  return static_cast<std::uint32_t>(3U + size);
}

struct CaptureState {
  std::uint32_t addr = 0;
  bool is_write = false;
  std::uint8_t size = 0;
  std::uint32_t return_cycles = 0;
};

std::uint32_t capture_cycles(void *ctx, std::uint32_t addr, bool is_write, std::uint8_t size) {
  auto *state = static_cast<CaptureState *>(ctx);
  state->addr = addr;
  state->is_write = is_write;
  state->size = size;
  return state->return_cycles;
}

void test_query_order_independence_for_same_tick_contenders() {
  busarb::Arbiter arb({fixed_cycles, nullptr});
  const busarb::BusRequest a{busarb::BusMasterId::SH2_A, 0x1000U, false, 4U, 10U};
  const busarb::BusRequest b{busarb::BusMasterId::SH2_B, 0x2000U, false, 4U, 10U};

  const auto ab_a = arb.query_wait(a);
  const auto ab_b = arb.query_wait(b);
  const auto ba_b = arb.query_wait(b);
  const auto ba_a = arb.query_wait(a);

  check(ab_a.should_wait == ba_a.should_wait && ab_a.wait_cycles == ba_a.wait_cycles,
        "query_wait for contender A should not depend on query call order");
  check(ab_b.should_wait == ba_b.should_wait && ab_b.wait_cycles == ba_b.wait_cycles,
        "query_wait for contender B should not depend on query call order");
}

void test_repeated_query_stability_without_commit() {
  busarb::Arbiter arb({fixed_cycles, nullptr});
  const busarb::BusRequest req{busarb::BusMasterId::SH2_A, 0x4321U, true, 2U, 100U};

  const auto first = arb.query_wait(req);
  for (int i = 0; i < 8; ++i) {
    const auto next = arb.query_wait(req);
    check(next.should_wait == first.should_wait, "repeated query_wait should keep stable should_wait value");
    check(next.wait_cycles == first.wait_cycles, "repeated query_wait should keep stable wait_cycles value");
  }
}

void test_pick_winner_uses_fixed_priority_for_three_way_same_tick() {
  busarb::Arbiter arb({fixed_cycles, nullptr});
  const std::array<busarb::BusRequest, 3> requests = {
      busarb::BusRequest{busarb::BusMasterId::SH2_B, 0x2000U, false, 4U, 20U},
      busarb::BusRequest{busarb::BusMasterId::SH2_A, 0x1000U, false, 4U, 20U},
      busarb::BusRequest{busarb::BusMasterId::DMA, 0x3000U, true, 4U, 20U},
  };

  const std::array<std::array<int, 3>, 3> orders = {{{0, 1, 2}, {2, 0, 1}, {1, 2, 0}}};
  for (const auto &order : orders) {
    std::vector<busarb::BusRequest> reqs;
    reqs.push_back(requests[static_cast<std::size_t>(order[0])]);
    reqs.push_back(requests[static_cast<std::size_t>(order[1])]);
    reqs.push_back(requests[static_cast<std::size_t>(order[2])]);

    const auto winner = arb.pick_winner(reqs);
    check(winner.has_value(), "pick_winner should return an index for non-empty request set");
    check(reqs[*winner].master_id == busarb::BusMasterId::DMA,
          "pick_winner should prefer DMA over SH2_A/SH2_B at same tick");
  }
}

void test_commit_determinism_and_wait_cycles() {
  busarb::Arbiter arb({fixed_cycles, nullptr}, {.same_address_contention = 0, .tie_turnaround = 0});
  const busarb::BusRequest req{busarb::BusMasterId::SH2_A, 0x1000U, false, 4U, 0U};

  arb.commit_grant(req, 0U);
  check(arb.bus_free_tick() == 7U, "commit_grant should advance bus_free_tick by callback-provided service cycles");

  const auto wait_now = arb.query_wait(busarb::BusRequest{busarb::BusMasterId::SH2_B, 0x2000U, false, 4U, 2U});
  check(wait_now.should_wait, "busy bus should request waiting when now_tick is before bus_free_tick");
  check(wait_now.wait_cycles == 5U, "wait_cycles should equal bus_free_tick - now_tick");

  arb.commit_grant(busarb::BusRequest{busarb::BusMasterId::DMA, 0x3000U, true, 1U, 7U}, 7U);
  check(arb.bus_free_tick() == 11U,
        "commit_grant with deterministic callback and same start tick should produce deterministic follow-up state");
}

void test_callback_arguments_passthrough_and_zero_cycle_clamp() {
  CaptureState capture{};
  capture.return_cycles = 0;
  busarb::Arbiter arb({capture_cycles, &capture});

  const busarb::BusRequest req{busarb::BusMasterId::DMA, 0xDEADBEEFU, true, 1U, 9U};
  arb.commit_grant(req, 9U);

  check(capture.addr == 0xDEADBEEFU, "access_cycles should receive request address");
  check(capture.is_write, "access_cycles should receive write flag");
  check(capture.size == 1U, "access_cycles should receive size_bytes");
  check(arb.bus_free_tick() == 10U, "zero callback cycles should be clamped to one deterministic tick");
}

void test_same_address_contention_penalty_applies() {
  busarb::Arbiter arb({fixed_cycles, nullptr}, {.same_address_contention = 2, .tie_turnaround = 0});
  const busarb::BusRequest a{busarb::BusMasterId::SH2_A, 0x2222U, false, 4U, 0U};
  const busarb::BusRequest b{busarb::BusMasterId::SH2_B, 0x2222U, false, 4U, 7U};
  arb.commit_grant(a, 0U);
  arb.commit_grant(b, 7U);
  check(arb.bus_free_tick() == 16U, "same-address consecutive grant should add contention penalty");
}

void test_different_address_has_no_contention_penalty() {
  busarb::Arbiter arb({fixed_cycles, nullptr}, {.same_address_contention = 2, .tie_turnaround = 0});
  arb.commit_grant(busarb::BusRequest{busarb::BusMasterId::SH2_A, 0x2000U, false, 4U, 0U}, 0U);
  arb.commit_grant(busarb::BusRequest{busarb::BusMasterId::SH2_B, 0x2004U, false, 4U, 7U}, 7U);
  check(arb.bus_free_tick() == 14U, "different-address consecutive grant should not add contention penalty");
}

void test_tie_turnaround_penalty_applies_only_after_tie_pick() {
  busarb::Arbiter arb({fixed_cycles, nullptr}, {.same_address_contention = 0, .tie_turnaround = 1});

  std::vector<busarb::BusRequest> tie_requests = {
      busarb::BusRequest{busarb::BusMasterId::SH2_A, 0x1000U, false, 4U, 10U},
      busarb::BusRequest{busarb::BusMasterId::SH2_B, 0x2000U, false, 4U, 10U},
  };
  const auto winner = arb.pick_winner(tie_requests);
  check(winner.has_value(), "winner expected for tie test");
  arb.commit_grant(tie_requests[*winner], 10U);
  check(arb.bus_free_tick() == 18U, "tie turnaround penalty should add one tick after tie");

  arb.commit_grant(busarb::BusRequest{busarb::BusMasterId::DMA, 0x3000U, false, 4U, 18U}, 18U);
  check(arb.bus_free_tick() == 25U, "non-tie commit should not keep tie turnaround penalty latched");
}

void test_round_robin_cpu_tie_break_alternates() {
  busarb::Arbiter arb({fixed_cycles, nullptr});
  std::vector<busarb::BusRequest> tie = {
      busarb::BusRequest{busarb::BusMasterId::SH2_A, 0x1000U, false, 4U, 1U},
      busarb::BusRequest{busarb::BusMasterId::SH2_B, 0x1004U, false, 4U, 1U},
  };

  auto first = arb.pick_winner(tie);
  check(first.has_value() && tie[*first].master_id == busarb::BusMasterId::SH2_A, "first CPU tie should pick SH2_A");
  arb.commit_grant(tie[*first], 1U);

  auto second = arb.pick_winner(tie);
  check(second.has_value() && tie[*second].master_id == busarb::BusMasterId::SH2_B,
        "second CPU tie should alternate to SH2_B");
  arb.commit_grant(tie[*second], 10U);

  auto third = arb.pick_winner(tie);
  check(third.has_value() && tie[*third].master_id == busarb::BusMasterId::SH2_A,
        "third CPU tie should alternate back to SH2_A");
}

} // namespace

int main() {
  test_query_order_independence_for_same_tick_contenders();
  test_repeated_query_stability_without_commit();
  test_pick_winner_uses_fixed_priority_for_three_way_same_tick();
  test_commit_determinism_and_wait_cycles();
  test_callback_arguments_passthrough_and_zero_cycle_clamp();
  test_same_address_contention_penalty_applies();
  test_different_address_has_no_contention_penalty();
  test_tie_turnaround_penalty_applies_only_after_tie_pick();
  test_round_robin_cpu_tie_break_alternates();
  std::cout << "busarb tests passed\n";
  return 0;
}
