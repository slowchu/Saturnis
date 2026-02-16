#include "bus/bus_arbiter.hpp"
#include "core/emulator.hpp"
#include "cpu/scripted_cpu.hpp"
#include "cpu/sh2_core.hpp"
#include "dev/devices.hpp"
#include "mem/memory.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

void check(bool cond, const std::string &msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    std::exit(1);
  }
}

void run_pair(saturnis::cpu::ScriptedCPU &cpu0, saturnis::cpu::ScriptedCPU &cpu1, saturnis::bus::BusArbiter &arbiter) {
  std::optional<saturnis::cpu::PendingBusOp> p0;
  std::optional<saturnis::cpu::PendingBusOp> p1;

  while (true) {
    arbiter.update_progress(0, cpu0.local_time() + 1);
    arbiter.update_progress(1, cpu1.local_time() + 1);

    if (!p0 && !cpu0.done()) {
      p0 = cpu0.produce();
    }
    if (!p1 && !cpu1.done()) {
      p1 = cpu1.produce();
    }

    if (!p0 && !p1 && cpu0.done() && cpu1.done()) {
      break;
    }

    std::vector<saturnis::bus::BusOp> pending_ops;
    std::vector<int> pending_cpu;
    std::vector<std::size_t> pending_script;
    if (p0) {
      pending_ops.push_back(p0->op);
      pending_cpu.push_back(0);
      pending_script.push_back(p0->script_index);
    }
    if (p1) {
      pending_ops.push_back(p1->op);
      pending_cpu.push_back(1);
      pending_script.push_back(p1->script_index);
    }

    if (pending_ops.empty()) {
      continue;
    }

    const auto committed = arbiter.commit_batch(pending_ops);
    for (const auto &result : committed) {
      const auto cpu = pending_cpu[result.input_index];
      const auto script_index = pending_script[result.input_index];
      if (cpu == 0) {
        cpu0.apply_response(script_index, result.response);
        p0.reset();
      } else {
        cpu1.apply_response(script_index, result.response);
        p1.reset();
      }
    }
  }
}

void test_tie_break_rr_determinism() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  for (std::uint64_t round = 0; round < 6U; ++round) {
    const saturnis::bus::BusOp op0{0, round * 100U, round, saturnis::bus::BusKind::Read, 0x2000U, 4, 0U};
    const saturnis::bus::BusOp op1{1, round * 100U, round, saturnis::bus::BusKind::Read, 0x3000U, 4, 0U};
    const auto committed = arbiter.commit_batch({op0, op1});
    check(committed.size() == 2U, "both contenders must commit");
    check(committed[0].op.cpu_id == static_cast<int>(round % 2U), "CPU grants should alternate on RR tie");
  }
}

void test_stall_applies_to_current_op() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Read, 0x20005000U, 4, 0, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {{saturnis::cpu::ScriptOpKind::Read, 0x20005004U, 4, 0, 0}});

  auto p0 = cpu0.produce();
  auto p1 = cpu1.produce();
  check(p0.has_value() && p1.has_value(), "both CPUs should emit blocking READ ops");
  const auto committed = arbiter.commit_batch({p0->op, p1->op});
  check(committed.size() == 2U, "both ops should commit");

  const auto winner_cpu = committed[0].op.cpu_id;
  const auto loser_cpu = committed[1].op.cpu_id;
  if (winner_cpu == 0) {
    cpu0.apply_response(p0->script_index, committed[0].response);
    cpu1.apply_response(p1->script_index, committed[1].response);
  } else {
    cpu1.apply_response(p1->script_index, committed[0].response);
    cpu0.apply_response(p0->script_index, committed[1].response);
  }

  check(committed[1].response.stall > committed[0].response.stall, "loser must absorb contention stall on the same op");
  const auto winner_time = (winner_cpu == 0) ? cpu0.local_time() : cpu1.local_time();
  const auto loser_time = (loser_cpu == 0) ? cpu0.local_time() : cpu1.local_time();
  check(loser_time > winner_time, "losing CPU virtual time should advance more on the contested read");
}

void test_no_host_order_dependence() {
  std::string baseline;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    for (std::uint64_t i = 0; i < 8U; ++i) {
      const saturnis::bus::BusOp op0{0, i * 20U, i, saturnis::bus::BusKind::Read, 0x20006000U, 4, 0U};
      const saturnis::bus::BusOp op1{1, i * 20U, i, saturnis::bus::BusKind::Read, 0x20006004U, 4, 0U};
      if ((run % 2) == 0) {
        (void)arbiter.commit_batch({op0, op1});
      } else {
        (void)arbiter.commit_batch({op1, op0});
      }
    }

    const auto current = trace.to_jsonl();
    if (run == 0) {
      baseline = current;
    } else {
      check(current == baseline, "trace must not depend on submission order across runs");
    }
  }
}

void test_commit_horizon_correctness() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  arbiter.update_progress(0, 4U);
  arbiter.update_progress(1, 100U);
  const saturnis::bus::BusOp blocked{1, 10U, 0, saturnis::bus::BusKind::Write, 0x7000U, 4, 0x11U};
  const auto blocked_try = arbiter.commit_batch({blocked});
  check(blocked_try.empty(), "arbiter must gate commits beyond commit horizon");

  const saturnis::bus::BusOp near_now{1, 3U, 1, saturnis::bus::BusKind::Write, 0x7004U, 4, 0x22U};
  const auto near_commit = arbiter.commit_batch({near_now});
  check(near_commit.size() == 1U, "committable op below horizon should make progress");

  arbiter.update_progress(0, 11U);
  const auto unblocked = arbiter.commit_batch({blocked});
  check(unblocked.size() == 1U, "op should commit once horizon moves forward");
}


void test_commit_horizon_requires_both_watermarks() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  arbiter.update_progress(0, 100U);

  const saturnis::bus::BusOp cpu0_op{0, 5U, 0, saturnis::bus::BusKind::Write, 0x7010U, 4, 0xAAU};
  const auto blocked = arbiter.commit_batch({cpu0_op});
  check(blocked.empty(), "horizon gating must block commits until both CPU watermarks are initialized");

  arbiter.update_progress(1, 200U);
  const auto committed = arbiter.commit_batch({cpu0_op});
  check(committed.size() == 1U, "op should commit once both CPU watermarks are available");
}


void test_commit_pending_retains_uncommitted_ops() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  arbiter.update_progress(0, 5U);
  arbiter.update_progress(1, 100U);

  std::vector<saturnis::bus::BusOp> pending{{0, 3U, 0, saturnis::bus::BusKind::Write, 0x7020U, 4, 0x10U},
                                            {1, 10U, 1, saturnis::bus::BusKind::Write, 0x7024U, 4, 0x20U}};

  const auto first = arbiter.commit_pending(pending);
  check(first.size() == 1U, "only op below current horizon should commit");
  check(first[0].input_index == 0U, "committed index should reference original pending position");
  check(pending.size() == 1U, "uncommitted op must remain queued");
  check(pending[0].phys_addr == 0x7024U, "remaining queued op should be the horizon-blocked op");

  arbiter.update_progress(0, 11U);
  const auto second = arbiter.commit_pending(pending);
  check(second.size() == 1U, "queued op should commit after horizon advances");
  check(second[0].input_index == 0U, "single remaining pending op should commit at index zero");
  check(pending.empty(), "pending queue should be empty after all ops commit");
}

void test_store_to_load_forwarding() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Write, 0x00002000U, 4, 0xAA55AA55U, 0},
                                      {saturnis::cpu::ScriptOpKind::Read, 0x00002000U, 4, 0, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {});
  run_pair(cpu0, cpu1, arbiter);
  check(cpu0.last_read().has_value() && *cpu0.last_read() == 0xAA55AA55U, "store-to-load forwarding value mismatch");
}

void test_barrier_does_not_change_contention_address_history() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp write_x{0, 0, 0, saturnis::bus::BusKind::Write, 0x1000U, 4, 0x1U};
  const saturnis::bus::BusOp barrier{0, 1, 1, saturnis::bus::BusKind::Barrier, 0U, 0, 0};
  const saturnis::bus::BusOp read_same{0, 2, 2, saturnis::bus::BusKind::Read, 0x1000U, 4, 0};

  (void)arbiter.commit(write_x);
  (void)arbiter.commit(barrier);
  const auto r = arbiter.commit(read_same);
  check(r.stall > 4U, "barrier must not alter last-address contention history");
}

void test_sh2_ifetch_cache_runahead() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  for (std::uint32_t addr = 0; addr < 16U; addr += 2U) {
    mem.write(addr, 2U, 0x0009U);
  }

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  const auto first = core.produce_until_bus(0, trace, 16);
  check(first.op.has_value(), "first fetch should be a bus miss");
  auto resp = arbiter.commit(*first.op);
  core.apply_ifetch_and_step(resp, trace);
  check(core.local_time() == (resp.stall + 1U), "IFETCH miss path must include intrinsic execute cost");

  const auto second = core.produce_until_bus(1, trace, 6);
  check(!second.op.has_value(), "subsequent IFETCHes in same line should be cache hits");
  check(second.executed > 0U, "cache run-ahead should execute instructions without bus");
}

} // namespace

int main() {
  test_tie_break_rr_determinism();
  test_stall_applies_to_current_op();
  test_no_host_order_dependence();
  test_commit_horizon_correctness();
  test_commit_horizon_requires_both_watermarks();
  test_commit_pending_retains_uncommitted_ops();
  test_store_to_load_forwarding();
  test_barrier_does_not_change_contention_address_history();
  test_sh2_ifetch_cache_runahead();
  std::cout << "saturnis kernel tests passed\n";
  return 0;
}
