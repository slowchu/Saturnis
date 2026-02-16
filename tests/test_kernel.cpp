#include "bus/bus_arbiter.hpp"
#include "core/emulator.hpp"
#include "cpu/scripted_cpu.hpp"
#include "cpu/sh2_core.hpp"
#include "dev/devices.hpp"
#include "mem/memory.hpp"

#include <cstdlib>
#include <iostream>
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
  while (!(cpu0.done() && cpu1.done())) {
    auto p0 = cpu0.produce();
    auto p1 = cpu1.produce();

    std::vector<std::pair<int, saturnis::cpu::PendingBusOp>> pending;
    if (p0) {
      pending.emplace_back(0, *p0);
    }
    if (p1) {
      pending.emplace_back(1, *p1);
    }
    if (pending.empty()) {
      break;
    }

    std::vector<saturnis::bus::BusOp> ops;
    ops.reserve(pending.size());
    for (const auto &e : pending) {
      ops.push_back(e.second.op);
    }

    const auto committed = arbiter.commit_batch(ops);
    for (const auto &result : committed) {
      const auto &e = pending[result.input_index];
      auto resp = result.response;
      if (e.first == 0) {
        cpu0.apply_response(e.second.script_index, resp);
      } else {
        cpu1.apply_response(e.second.script_index, resp);
      }
    }
  }
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

void test_cache_hit_vs_uncached_alias() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x3000U, 4, 1U);
  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Write, 0x00003000U, 4, 2U, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {{saturnis::cpu::ScriptOpKind::Read, 0x00003000U, 4, 0, 0},
                                      {saturnis::cpu::ScriptOpKind::Read, 0x00003000U, 4, 0, 0},
                                      {saturnis::cpu::ScriptOpKind::Read, 0x20003000U, 4, 0, 0}});
  run_pair(cpu0, cpu1, arbiter);
  check(cpu1.last_read().has_value() && *cpu1.last_read() == 2U, "uncached alias should observe committed value");
}

void test_cache_line_fill_adjacent_read() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x8000U, 4, 0x11111111U);
  mem.write(0x8004U, 4, 0x22222222U);
  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Read, 0x00008000U, 4, 0, 0},
                                      {saturnis::cpu::ScriptOpKind::Read, 0x00008004U, 4, 0, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {});
  run_pair(cpu0, cpu1, arbiter);
  check(cpu0.last_read().has_value() && *cpu0.last_read() == 0x22222222U, "cache line fill should preserve adjacent bytes");
}

void test_contention_ordering() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Write, 0x00004000U, 4, 10U, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {{saturnis::cpu::ScriptOpKind::Write, 0x00004000U, 4, 20U, 0}});
  run_pair(cpu0, cpu1, arbiter);
  check(mem.read(0x4000U, 4) == 20U, "cpu1 write should commit after cpu0 due to tie-breaker");
}

void test_arbiter_ordering_rule_without_caller_sort() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp late_cpu1{1, 10, 0, saturnis::bus::BusKind::Write, 0x7000U, 4, 11U};
  const saturnis::bus::BusOp early_cpu0{0, 9, 0, saturnis::bus::BusKind::Write, 0x7000U, 4, 22U};
  const auto results = arbiter.commit_batch({late_cpu1, early_cpu0});
  check(results.size() == 2U, "commit_batch should return both results");
  check(results[0].op.cpu_id == 0 && results[0].op.req_time == 9U, "arbiter must order by req_time first");
  check(mem.read(0x7000U, 4) == 11U, "later write should win after deterministic ordering");
}

void test_stall_propagation() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Write, 0x00005000U, 4, 1U, 0},
                                      {saturnis::cpu::ScriptOpKind::Compute, 0, 0, 0, 1},
                                      {saturnis::cpu::ScriptOpKind::Write, 0x00005000U, 4, 2U, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {});
  run_pair(cpu0, cpu1, arbiter);
  check(cpu0.local_time() > 3U, "stall should increase future request time");
}

void test_mmio_strong_ordering() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Write, 0x05F00020U, 4, 0x11U, 0},
                                      {saturnis::cpu::ScriptOpKind::Write, 0x05F00024U, 4, 0x22U, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {});
  run_pair(cpu0, cpu1, arbiter);
  const auto &writes = dev.writes();
  check(writes.size() == 2U, "expected 2 MMIO writes");
  check(writes[0].addr == 0x05F00020U && writes[1].addr == 0x05F00024U, "MMIO order within CPU must be preserved");
}

void test_barrier_is_synchronizing_bus_op() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Compute, 0, 0, 0, 3},
                                      {saturnis::cpu::ScriptOpKind::Barrier, 0, 0, 0, 0},
                                      {saturnis::cpu::ScriptOpKind::Write, 0x00009000U, 4, 0x77U, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {});
  run_pair(cpu0, cpu1, arbiter);
  check(mem.read(0x9000U, 4) == 0x77U, "barrier should not deadlock scripted execution");
}

void test_barrier_is_not_memory_read() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0U, 4, 0x12345678U);
  saturnis::cpu::ScriptedCPU cpu0(0, {{saturnis::cpu::ScriptOpKind::Barrier, 0, 0, 0, 0}});
  saturnis::cpu::ScriptedCPU cpu1(1, {});
  run_pair(cpu0, cpu1, arbiter);
  check(mem.read(0x0U, 4) == 0x12345678U, "barrier must not mutate or read through regular memory side effects");
}

void test_barrier_does_not_change_contention_address_history() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp write_x{0, 0, 0, saturnis::bus::BusKind::Write, 0x1000U, 4, 0x1U};
  const saturnis::bus::BusOp barrier{0, 1, 1, saturnis::bus::BusKind::Barrier, 0U, 0, 0};
  const saturnis::bus::BusOp read_zero{0, 2, 2, saturnis::bus::BusKind::Read, 0U, 4, 0};

  (void)arbiter.commit(write_x);
  (void)arbiter.commit(barrier);
  const auto r = arbiter.commit(read_zero);
  check(r.stall == 2U, "barrier must not alter last-address contention history");
}

void test_sh2_ifetch_cache_runahead() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  // Fill a cache line with NOPs; one miss should allow multiple local IFETCH hits.
  for (std::uint32_t addr = 0; addr < 16U; addr += 2U) {
    mem.write(addr, 2U, 0x0009U);
  }

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  const auto first = core.produce_until_bus(0, trace, 16);
  check(first.op.has_value(), "first fetch should be a bus miss");
  auto resp = arbiter.commit(*first.op);
  core.apply_ifetch_and_step(resp, trace);

  const auto second = core.produce_until_bus(1, trace, 6);
  check(!second.op.has_value(), "subsequent IFETCHes in same line should be cache hits");
  check(second.executed > 0U, "cache run-ahead should execute instructions without bus");
}

void test_sh2_runahead_budget_is_respected() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  for (std::uint32_t addr = 0; addr < 16U; addr += 2U) {
    mem.write(addr, 2U, 0x0009U);
  }

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);
  const auto first = core.produce_until_bus(0, trace, 1);
  check(first.op.has_value(), "first IFETCH should miss cache");
  const auto resp = arbiter.commit(*first.op);
  core.apply_ifetch_and_step(resp, trace);

  const auto before = core.executed_instructions();
  const auto second = core.produce_until_bus(1, trace, 3);
  const auto after = core.executed_instructions();
  check(!second.op.has_value(), "cached runahead should stay local when line already present");
  check(after - before <= 3U, "produce_until_bus must honor runahead budget");
}

void test_sh2_data_local_view_store_and_load() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  // Program:
  // mov #0x10,r1
  // mov #5,r2
  // mov.l r2,@r1
  // mov.l @r1,r3
  // nop
  mem.write(0x00U, 2U, 0xE110U);
  mem.write(0x02U, 2U, 0xE205U);
  mem.write(0x04U, 2U, 0x2122U);
  mem.write(0x06U, 2U, 0x6312U);
  mem.write(0x08U, 2U, 0x0009U);

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  std::uint64_t seq = 0;
  while (core.executed_instructions() < 5U) {
    const auto produced = core.produce_until_bus(seq++, trace, 8);
    if (!produced.op.has_value()) {
      continue;
    }
    const auto resp = arbiter.commit(*produced.op);
    core.apply_ifetch_and_step(resp, trace);
  }

  check(core.reg(3) == 5U, "mov.l load should observe local/committed write value");
  check(mem.read(0x10U, 4U) == 5U, "mov.l store should become globally visible after arbiter commit");
}

void test_sh2_data_byte_word_and_predec_postinc() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  // Program:
  // mov #0x24,r1
  // mov #0x7f,r2
  // mov.b r2,@r1
  // mov.b @r1,r3
  // mov #0x28,r4
  // mov #0x34,r5
  // mov #0x11,r6
  // mov.b r6,@-r5
  // mov.b @r5+,r7
  // mov #0x12,r8
  // mov.w r8,@r4
  // mov.w @r4,r9
  // nop
  const std::uint16_t prog[] = {0xE124U, 0xE27FU, 0x2120U, 0x6310U, 0xE428U, 0xE534U, 0xE611U,
                                0x2564U, 0x6754U, 0xE812U, 0x2481U, 0x6941U, 0x0009U};
  for (std::size_t i = 0; i < (sizeof(prog) / sizeof(prog[0])); ++i) {
    mem.write(static_cast<std::uint32_t>(i * 2U), 2U, prog[i]);
  }

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  std::uint64_t seq = 0;
  while (core.executed_instructions() < (sizeof(prog) / sizeof(prog[0]))) {
    const auto produced = core.produce_until_bus(seq++, trace, 8);
    if (!produced.op.has_value()) {
      continue;
    }
    const auto resp = arbiter.commit(*produced.op);
    core.apply_ifetch_and_step(resp, trace);
  }

  check((core.reg(3) & 0xFFU) == 0x7FU, "mov.b load should read stored byte");
  check((core.reg(7) & 0xFFU) == 0x11U, "mov.b post-inc load should read pre-dec stored byte");
  check(core.reg(5) == 0x34U, "post-inc should restore register after pre-dec+load pair");
  check((core.reg(9) & 0xFFFFU) == 0x0012U, "mov.w load should read stored word");
  check((mem.read(0x24U, 1U) & 0xFFU) == 0x7FU, "byte store should commit globally");
  check((mem.read(0x28U, 2U) & 0xFFFFU) == 0x0012U, "word store should commit globally");
}

} // namespace

int main() {
  test_store_to_load_forwarding();
  test_cache_hit_vs_uncached_alias();
  test_cache_line_fill_adjacent_read();
  test_contention_ordering();
  test_arbiter_ordering_rule_without_caller_sort();
  test_stall_propagation();
  test_mmio_strong_ordering();
  test_barrier_is_synchronizing_bus_op();
  test_barrier_is_not_memory_read();
  test_barrier_does_not_change_contention_address_history();
  test_sh2_ifetch_cache_runahead();
  test_sh2_runahead_budget_is_respected();
  test_sh2_data_local_view_store_and_load();
  test_sh2_data_byte_word_and_predec_postinc();
  std::cout << "saturnis kernel tests passed\n";
  return 0;
}
