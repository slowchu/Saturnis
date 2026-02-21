#include "busarb/busarb.hpp"

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

void test_pick_winner_uses_fixed_priority() {
  busarb::Arbiter arb({fixed_cycles, nullptr});
  const std::vector<busarb::BusRequest> reqs{
      {busarb::BusMasterId::SH2_B, 0x2000U, false, 4U, 20U},
      {busarb::BusMasterId::SH2_A, 0x1000U, false, 4U, 20U},
      {busarb::BusMasterId::DMA, 0x3000U, true, 4U, 20U},
  };

  const auto winner = arb.pick_winner(reqs);
  check(winner.has_value(), "pick_winner should return an index for non-empty request set");
  check(*winner == 2U, "pick_winner should prefer DMA over SH2_A/SH2_B at same tick");
}

void test_commit_determinism_and_wait_cycles() {
  busarb::Arbiter arb({fixed_cycles, nullptr});
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

} // namespace

int main() {
  test_query_order_independence_for_same_tick_contenders();
  test_pick_winner_uses_fixed_priority();
  test_commit_determinism_and_wait_cycles();
  std::cout << "busarb tests passed\n";
  return 0;
}
