#include "busarb/busarb.hpp"

#include <cstdint>
#include <iostream>

namespace {

std::uint32_t harness_cycles(void *, std::uint32_t, bool, std::uint8_t size) {
  return static_cast<std::uint32_t>(2U + size);
}

} // namespace

int main() {
  busarb::Arbiter arb({harness_cycles, nullptr});
  const busarb::BusRequest req{busarb::BusMasterId::SH2_A, 0x1000U, false, 4U, 0U};
  const auto wait = arb.query_wait(req);
  if (wait.should_wait || wait.wait_cycles != 0U) {
    return 1;
  }
  arb.commit_grant(req, 0U);
  if (arb.bus_free_tick() != 6U) {
    return 1;
  }
  std::cout << "busarb link harness passed\n";
  return 0;
}
