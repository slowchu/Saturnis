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


void test_commit_horizon_requires_both_progress_watermarks() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  arbiter.update_progress(0, 100U);

  const saturnis::bus::BusOp cpu0_op{0, 5U, 0, saturnis::bus::BusKind::Write, 0x7010U, 4, 0xAAU};
  const auto blocked = arbiter.commit_batch({cpu0_op});
  check(blocked.empty(), "horizon gating must block commits until both CPU progress watermarks are initialized");

  arbiter.update_progress(1, 200U);
  const auto committed = arbiter.commit_batch({cpu0_op});
  check(committed.size() == 1U, "op should commit once both CPU progress watermarks are available");
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

void test_commit_pending_waits_for_both_progress_watermarks() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  arbiter.update_progress(0, 100U);

  std::vector<saturnis::bus::BusOp> pending{{0, 5U, 0, saturnis::bus::BusKind::Write, 0x7030U, 4, 0xA0U}};
  const auto blocked = arbiter.commit_pending(pending);
  check(blocked.empty(), "commit_pending should defer work until both CPU progress watermarks exist");
  check(pending.size() == 1U, "pending queue should be unchanged while horizon is unsafe");

  arbiter.update_progress(1, 200U);
  const auto committed = arbiter.commit_pending(pending);
  check(committed.size() == 1U, "pending op should commit once both CPU progress watermarks are available");
  check(pending.empty(), "pending queue should drain after commit succeeds");
}


void test_commit_horizon_interleaves_mmio_and_ram_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  arbiter.update_progress(0, 6U);
  arbiter.update_progress(1, 6U);
  std::vector<saturnis::bus::BusOp> pending{{1, 5U, 0, saturnis::bus::BusKind::Write, 0x1200U, 4, 0xA1U},
                                            {0, 5U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x2U}};

  const auto committed = arbiter.commit_pending(pending);
  check(committed.size() == 2U, "interleaved MMIO/RAM pending ops should both commit below horizon");
  check(committed[0].op.cpu_id == 0 && committed[1].op.cpu_id == 1,
        "equal-time interleaved MMIO/RAM ops should commit in deterministic policy order");
  check(pending.empty(), "pending queue should drain once interleaved MMIO/RAM ops commit");

  check(mem.read(0x1200U, 4U) == 0xA1U, "RAM op in interleaved commit test should be applied deterministically");
  const auto source = arbiter.commit({0, 7U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source.value == 0x2U, "MMIO op in interleaved commit test should be applied deterministically");
}

void test_commit_pending_preserves_order_of_remaining_ops() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  arbiter.update_progress(0, 6U);
  arbiter.update_progress(1, 100U);

  std::vector<saturnis::bus::BusOp> pending{{0, 1U, 0, saturnis::bus::BusKind::Write, 0x7040U, 4, 0x10U},
                                            {1, 20U, 1, saturnis::bus::BusKind::Write, 0x7044U, 4, 0x20U},
                                            {0, 30U, 2, saturnis::bus::BusKind::Write, 0x7048U, 4, 0x30U}};

  const auto first = arbiter.commit_pending(pending);
  check(first.size() == 1U, "only safe-horizon op should commit from mixed pending queue");
  check(pending.size() == 2U, "two horizon-blocked ops should stay pending");
  check(pending[0].phys_addr == 0x7044U, "first blocked op should keep original relative order");
  check(pending[1].phys_addr == 0x7048U, "second blocked op should keep original relative order");
}


void test_commit_horizon_cycles_progress_with_mixed_pending_ram_and_mmio() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 2U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000003U},
                                            {1, 3U, 1, saturnis::bus::BusKind::Write, 0x7080U, 4, 0xA5A5A5A5U},
                                            {0, 8U, 2, saturnis::bus::BusKind::Write, 0x7084U, 4, 0x5A5A5A5AU},
                                            {1, 9U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000002U}};

  arbiter.update_progress(0, 5U);
  arbiter.update_progress(1, 4U);
  const auto first = arbiter.commit_pending(pending);
  check(first.size() == 2U, "first horizon cycle should commit only early mixed MMIO/RAM ops");
  check(pending.size() == 2U, "later mixed MMIO/RAM ops should remain pending after first horizon cycle");

  arbiter.update_progress(1, 7U);
  const auto second = arbiter.commit_pending(pending);
  check(second.empty(), "advancing one CPU progress alone should keep mixed pending queue horizon-blocked");
  check(pending.size() == 2U, "pending queue should remain unchanged while horizon still below queued req_time");

  arbiter.update_progress(0, 10U);
  arbiter.update_progress(1, 10U);
  const auto third = arbiter.commit_pending(pending);
  check(third.size() == 2U, "final horizon cycle should commit remaining mixed MMIO/RAM ops");
  check(pending.empty(), "pending queue should drain after mixed MMIO/RAM horizon becomes committable");

  check(mem.read(0x7080U, 4U) == 0xA5A5A5A5U, "first-cycle RAM write should commit deterministically");
  check(mem.read(0x7084U, 4U) == 0x5A5A5A5AU, "final-cycle RAM write should commit deterministically");
  const auto source_after = arbiter.commit({0, 11U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_after.value == 0x00000001U,
        "mixed pending MMIO set/clear operations should deterministically resolve across horizon cycles");
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


void test_mmio_write_is_visible_to_subsequent_reads() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp write_mmio{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05F00020U, 4, 0x12345678U};
  const saturnis::bus::BusOp read_mmio{1, 1U, 0, saturnis::bus::BusKind::MmioRead, 0x05F00020U, 4, 0U};

  (void)arbiter.commit(write_mmio);
  const auto r = arbiter.commit(read_mmio);
  check(r.value == 0x12345678U, "MMIO read should return last written 32-bit register value");
}

void test_mmio_subword_write_updates_correct_lane() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05F00024U, 4, 0x11223344U});
  (void)arbiter.commit({1, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05F00025U, 1, 0xAAU});

  const auto byte_read = arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05F00025U, 1, 0U});
  check(byte_read.value == 0xAAU, "byte MMIO read should observe byte-lane write");

  const auto word_read = arbiter.commit({1, 3U, 3, saturnis::bus::BusKind::MmioRead, 0x05F00024U, 4, 0U});
  check(word_read.value == 0x1122AA44U, "subword MMIO write should patch only targeted byte lane");
}

void test_display_status_register_is_read_only_and_ready() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const auto initial = arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioRead, 0x05F00010U, 4, 0U});
  check(initial.value == 0x1U, "display status should expose deterministic ready bit");

  (void)arbiter.commit({1, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05F00010U, 4, 0xFFFFFFFFU});
  (void)arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05F00010U, 1, 0x00U});

  const auto after = arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioRead, 0x05F00010U, 4, 0U});
  check(after.value == 0x1U, "display status writes should not overwrite read-only ready bit");

  const auto low_byte = arbiter.commit({1, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05F00010U, 1, 0U});
  check(low_byte.value == 0x1U, "display-status low byte should retain ready bit after writes");

  const auto high_byte = arbiter.commit({1, 5U, 5, saturnis::bus::BusKind::MmioRead, 0x05F00013U, 1, 0U});
  check(high_byte.value == 0U, "upper display-status byte lanes should stay clear");

  const auto &writes = dev.writes();
  check(writes.size() == 2U, "display-status writes should still be logged for traceability");
  check(writes[0].addr == 0x05F00010U && writes[1].addr == 0x05F00010U,
        "write log should keep display-status register address");
}

void test_scu_dma_register_file_masks_and_lane_semantics_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const auto dma_src_reset = arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioRead, 0x05FE0020U, 4, 0U});
  const auto dma_size_reset = arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioRead, 0x05FE0028U, 4, 0U});
  const auto dma_ctrl_reset = arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE002CU, 4, 0U});
  check(dma_src_reset.value == 0U, "SCU DMA0 source register should reset deterministically");
  check(dma_size_reset.value == 0U, "SCU DMA0 size register should reset deterministically");
  check(dma_ctrl_reset.value == 0U, "SCU DMA0 control register should reset deterministically");

  (void)arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE0028U, 4, 0xFFF12345U});
  const auto dma_size_masked = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE0028U, 4, 0U});
  check(dma_size_masked.value == 0x00012345U, "SCU DMA0 size register should mask to low 20 bits");

  (void)arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioWrite, 0x05FE002DU, 1, 0xFFU});
  const auto dma_ctrl_lane = arbiter.commit({0, 6U, 6, saturnis::bus::BusKind::MmioRead, 0x05FE002CU, 4, 0U});
  check((dma_ctrl_lane.value & ~0x17U) == 0U, "SCU DMA0 control register should keep only writable low control bits");

  const auto json = trace.to_jsonl();
  check(json.find("\"kind\":\"MMIO_WRITE\"") != std::string::npos,
        "SCU DMA register interactions should produce deterministic MMIO_WRITE commits in trace");
  check(json.find("\"phys\":100532264") != std::string::npos,
        "SCU DMA size-register trace should include deterministic MMIO phys address checkpoint");
}

void test_scu_ims_register_masks_to_low_16_bits() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const auto initial = arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioRead, 0x05FE00A0U, 4, 0U});
  check(initial.value == 0U, "SCU IMS reset value should be deterministic zero");

  (void)arbiter.commit({1, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0xA5A5BEEF});
  const auto after_word_write = arbiter.commit({1, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00A0U, 4, 0U});
  check(after_word_write.value == 0x0000BEEFU, "SCU IMS should ignore upper 16 bits on 32-bit writes");

  (void)arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00A2U, 2, 0xFFFFU});
  const auto after_high_half_write = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00A0U, 4, 0U});
  check(after_high_half_write.value == 0x0000BEEFU, "SCU IMS high-halfword writes should be masked out");

  (void)arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioWrite, 0x05FE00A1U, 1, 0x11U});
  const auto low_byte = arbiter.commit({1, 6U, 6, saturnis::bus::BusKind::MmioRead, 0x05FE00A1U, 1, 0U});
  check(low_byte.value == 0x11U, "SCU IMS byte-lane writes in writable region should be visible");
}




void test_scu_interrupt_pending_respects_mask_and_clear() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const auto initial_status = arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(initial_status.value == 0U, "SCU IST should start with no pending interrupts");

  (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00A4U, 4, 0x00000005U});
  const auto pending_visible = arbiter.commit({1, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(pending_visible.value == 0x00000005U, "SCU IST should expose pending bits when IMS mask is clear");

  (void)arbiter.commit({1, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000001U});
  const auto masked_status = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(masked_status.value == 0x00000004U, "SCU IST should suppress masked pending interrupt bits");

  (void)arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000004U});
  const auto after_clear = arbiter.commit({1, 6U, 6, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(after_clear.value == 0U, "SCU IST clear register should drop matching pending bits");

  (void)arbiter.commit({1, 7U, 7, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000000U});
  const auto unmasked_after_clear = arbiter.commit({0, 8U, 8, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(unmasked_after_clear.value == 0x00000001U,
        "SCU IST should retain masked pending bits until explicitly cleared");

  (void)arbiter.commit({0, 9U, 9, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000001U});
  const auto final_status = arbiter.commit({1, 10U, 10, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(final_status.value == 0U, "SCU IST should be empty after clearing remaining pending bits");
}


void test_scu_interrupt_source_pending_wires_into_ist() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000012U});
  const auto source_latched = arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_latched.value == 0x12U, "SCU synthetic source register should latch pending bits");

  const auto ist_unmasked = arbiter.commit({1, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(ist_unmasked.value == 0x12U, "SCU IST should include synthetic source pending bits");

  (void)arbiter.commit({1, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000010U});
  const auto ist_masked = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(ist_masked.value == 0x02U, "SCU IMS should mask synthetic source bits in IST view");

  (void)arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000002U});
  const auto source_after_source_clear = arbiter.commit({1, 6U, 6, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_after_source_clear.value == 0x10U, "SCU synthetic source clear should drop selected bits");

  (void)arbiter.commit({1, 7U, 7, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000010U});
  const auto ist_after_ack = arbiter.commit({0, 8U, 8, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(ist_after_ack.value == 0U, "SCU IST clear should acknowledge remaining synthetic source bits");
}






void test_scu_synthetic_source_mixed_size_concurrent_clears_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x000012FFU});

  const saturnis::bus::BusOp cpu0_half_clear{0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 2, 0x00F0U};
  const saturnis::bus::BusOp cpu1_byte_clear{1, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x12U};
  const auto committed = arbiter.commit_batch({cpu0_half_clear, cpu1_byte_clear});

  check(committed.size() == 2U, "mixed-size concurrent clear batch should commit both SCU source clears");
  check(committed[0].op.cpu_id == 0 && committed[1].op.cpu_id == 1,
        "mixed-size concurrent clear arbitration should remain deterministic");

  const auto source_after = arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_after.value == 0x0000000FU,
        "mixed-size concurrent source clears should deterministically produce the expected remaining bits");
}


void test_scu_synthetic_source_mixed_size_overlapping_clears_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x0000FFFFU});

  const saturnis::bus::BusOp cpu0_half_clear{0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 2, 0x0FF0U};
  const saturnis::bus::BusOp cpu1_byte_clear{1, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x0FU};
  const auto committed = arbiter.commit_batch({cpu0_half_clear, cpu1_byte_clear});

  check(committed.size() == 2U, "overlapping mixed-size clear batch should commit both SCU source clears");
  check(committed[0].op.cpu_id == 0 && committed[1].op.cpu_id == 1,
        "overlapping mixed-size clear arbitration should remain deterministic");

  const auto source_after = arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_after.value == 0x0000F00FU,
        "overlapping mixed-size source clears should deterministically resolve overlapping clear masks");
}

void test_scu_synthetic_source_mixed_size_contention_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp cpu0_byte_set{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x12U};
  const saturnis::bus::BusOp cpu1_half_set{1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 2, 0x0034U};
  const auto first = arbiter.commit_batch({cpu0_byte_set, cpu1_half_set});

  check(first.size() == 2U, "mixed-size contention batch should commit both SCU source writes");
  check(first[0].op.cpu_id == 0 && first[1].op.cpu_id == 1,
        "mixed-size contention should use deterministic CPU arbitration ordering");

  const auto after_first = arbiter.commit({0, 1U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(after_first.value == 0x00001234U,
        "mixed-size source writes should deterministically merge byte/halfword lane contributions");

  const saturnis::bus::BusOp cpu0_word_clear{0, 2U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00001000U};
  const saturnis::bus::BusOp cpu1_byte_clear{1, 2U, 4, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 1, 0x00000004U};
  const auto second = arbiter.commit_batch({cpu0_word_clear, cpu1_byte_clear});

  check(second.size() == 2U, "mixed-size clear batch should commit both SCU source clear writes");
  check(second[0].op.cpu_id == 1 && second[1].op.cpu_id == 0,
        "mixed-size follow-up contention should rotate deterministic round-robin winner");

  const auto after_second = arbiter.commit({1, 3U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(after_second.value == 0x00000230U,
        "mixed-size source clear writes should deterministically clear targeted bits across lanes");
}

void test_scu_synthetic_source_mixed_cpu_contention_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp cpu0_set{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000001U};
  const saturnis::bus::BusOp cpu1_set{1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000002U};
  const auto committed = arbiter.commit_batch({cpu0_set, cpu1_set});

  check(committed.size() == 2U, "both contending SCU source writes should commit deterministically");
  check(committed[0].op.cpu_id == 0 && committed[1].op.cpu_id == 1,
        "mixed-CPU SCU source contention should resolve in deterministic round-robin order");

  const auto source_state = arbiter.commit({0, 1U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_state.value == 0x00000003U,
        "mixed-CPU SCU source writes should deterministically accumulate pending bits");
}

void test_scu_synthetic_source_mmio_stall_is_stable_across_runs() {
  std::vector<saturnis::core::Tick> baseline_stalls;

  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    const saturnis::bus::BusOp cpu0_set{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000001U};
    const saturnis::bus::BusOp cpu1_clear{1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000001U};
    const auto committed = arbiter.commit_batch({cpu0_set, cpu1_clear});

    check(committed.size() == 2U, "stall stability scenario should commit both MMIO writes");
    check(committed[0].response.stall > 0U && committed[1].response.stall > committed[0].response.stall,
          "stall profile should reflect deterministic tie/serialization behavior");

    std::vector<saturnis::core::Tick> stalls{committed[0].response.stall, committed[1].response.stall};
    if (run == 0) {
      baseline_stalls = stalls;
    } else {
      check(stalls == baseline_stalls,
            "SCU synthetic-source MMIO commit stall fields should remain stable under repeated runs");
    }
  }
}

void test_scu_interrupt_source_subword_writes_apply_lane_masks() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x12U});
  const auto after_high_byte_set = arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(after_high_byte_set.value == 0x00001200U,
        "SCU source set byte-lane writes should land in the addressed low-16 byte lane");

  (void)arbiter.commit({1, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00AEU, 2, 0xFFFFU});
  const auto after_high_half_set = arbiter.commit({1, 3U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(after_high_half_set.value == 0x00001200U,
        "SCU source set high-halfword writes should be masked out of the low-16 source state");

  (void)arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 1, 0x34U});
  const auto after_low_byte_set = arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(after_low_byte_set.value == 0x00001234U,
        "SCU source set low-byte writes should combine deterministically with existing source bits");

  (void)arbiter.commit({1, 6U, 6, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x10U});
  const auto after_high_byte_clear = arbiter.commit({1, 7U, 7, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(after_high_byte_clear.value == 0x00000234U,
        "SCU source clear byte-lane writes should clear only selected bits in addressed lane");

  (void)arbiter.commit({0, 8U, 8, saturnis::bus::BusKind::MmioWrite, 0x05FE00B2U, 2, 0xFFFFU});
  const auto after_high_half_clear = arbiter.commit({0, 9U, 9, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(after_high_half_clear.value == 0x00000234U,
        "SCU source clear high-halfword writes should be masked out of the low-16 source state");
}

void test_scu_interrupt_source_write_log_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 5U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000012U});
  (void)arbiter.commit({1, 6U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000002U});
  (void)arbiter.commit({0, 7U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000010U});

  const auto &writes = dev.writes();
  check(writes.size() == 3U, "SCU synthetic-source transitions should produce deterministic write-log cardinality");

  check(writes[0].cpu == 0 && writes[0].addr == 0x05FE00ACU && writes[0].value == 0x00000012U,
        "first SCU synthetic-source transition should be logged with deterministic metadata");
  check(writes[1].cpu == 1 && writes[1].addr == 0x05FE00B0U && writes[1].value == 0x00000002U,
        "second SCU synthetic-source transition should be logged with deterministic metadata");
  check(writes[2].cpu == 0 && writes[2].addr == 0x05FE00A8U && writes[2].value == 0x00000010U,
        "third SCU synthetic-source transition should be logged with deterministic metadata");
  check(writes[0].t < writes[1].t && writes[1].t < writes[2].t,
        "SCU synthetic-source transition timestamps should be strictly monotonic in deterministic commit order");
}


void test_scu_synthetic_source_mmio_commit_trace_order_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000012U});
  (void)arbiter.commit({1, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000002U});
  (void)arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000010U});

  const auto jsonl = trace.to_jsonl();
  const auto pos_set = jsonl.find("\"kind\":\"MMIO_WRITE\",\"phys\":100532396");
  const auto pos_source_clear = jsonl.find("\"kind\":\"MMIO_WRITE\",\"phys\":100532400");
  const auto pos_ist_clear = jsonl.find("\"kind\":\"MMIO_WRITE\",\"phys\":100532392");

  check(pos_set != std::string::npos && pos_source_clear != std::string::npos && pos_ist_clear != std::string::npos,
        "SCU synthetic-source MMIO writes should appear in trace JSONL commits");
  check(pos_set < pos_source_clear && pos_source_clear < pos_ist_clear,
        "SCU synthetic-source MMIO writes should appear in deterministic commit order in trace JSONL");
}

void test_smpc_status_register_is_read_only_and_ready() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const auto initial = arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioRead, 0x05D00080U, 4, 0U});
  check(initial.value == 0x1U, "SMPC status should expose deterministic ready bit");

  (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05D00080U, 4, 0xFFFFFFFFU});
  const auto after = arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05D00080U, 4, 0U});
  check(after.value == 0x1U, "SMPC status should remain read-only");
}

void test_vdp2_tvmd_register_masks_to_low_16_bits() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05F80000U, 4, 0xABCD1234U});
  const auto after = arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioRead, 0x05F80000U, 4, 0U});
  check(after.value == 0x00001234U, "VDP2 TVMD should only latch low 16 bits");
}

void test_vdp2_tvstat_register_is_read_only_with_deterministic_status() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const auto initial = arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioRead, 0x05F80004U, 4, 0U});
  check(initial.value == 0x00000008U, "VDP2 TVSTAT should expose deterministic default status bits");

  (void)arbiter.commit({1, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05F80004U, 4, 0xFFFFFFFFU});
  const auto after = arbiter.commit({1, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05F80004U, 4, 0U});
  check(after.value == 0x00000008U, "VDP2 TVSTAT should remain read-only after writes");

  const auto low_half = arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioRead, 0x05F80004U, 2, 0U});
  check(low_half.value == 0x0008U, "VDP2 TVSTAT low halfword should keep deterministic status bits");

  const auto high_half = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05F80006U, 2, 0U});
  check(high_half.value == 0U, "VDP2 TVSTAT high halfword should stay clear");
}

void test_scsp_mcier_register_masks_to_low_11_bits() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05C00000U, 4, 0xFFFFFFFFU});
  const auto after = arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioRead, 0x05C00000U, 4, 0U});
  check(after.value == 0x000007FFU, "SCSP MCIER should apply deterministic writable-bit mask");
}


void test_sh2_movw_memory_read_executes_via_bus_with_sign_extend() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0002U, 2U, 0x6211U); // MOV.W @R1,R2
  mem.write(0x0040U, 2U, 0xFF80U);

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);

  check(core.pc() == 0x0004U, "MOV.W @Rm,Rn should retire and advance PC");
  check(core.reg(2) == 0xFFFFFF80U, "MOV.W @Rm,Rn should sign-extend 16-bit data into destination register");
}

void test_sh2_movw_memory_write_executes_via_bus_low_halfword_only() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE144U); // MOV #0x44,R1
  mem.write(0x0002U, 2U, 0xE2FFU); // MOV #-1,R2
  mem.write(0x0004U, 2U, 0x2121U); // MOV.W R2,@R1

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);

  check(core.pc() == 0x0006U, "MOV.W Rm,@Rn should retire and advance PC");
  check(mem.read(0x0044U, 2U) == 0xFFFFU, "MOV.W Rm,@Rn should store the low 16 bits to memory");
}

void test_sh2_movl_memory_read_executes_via_bus() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0002U, 2U, 0x6212U); // MOV.L @R1,R2
  mem.write(0x0004U, 2U, 0x0009U); // NOP
  mem.write(0x0040U, 4U, 0xCAFEBABEU);

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  check(core.pc() == 0x0002U, "MOV #imm should retire and advance PC");

  core.step(arbiter, trace, 1);
  check(core.pc() == 0x0004U, "MOV.L @Rm,Rn should retire as a blocking bus data read");
  check(core.reg(2) == 0xCAFEBABEU, "MOV.L @Rm,Rn should load register from committed memory");
}

void test_sh2_movl_memory_write_executes_via_bus() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE144U); // MOV #0x44,R1
  mem.write(0x0002U, 2U, 0xE27FU); // MOV #0x7F,R2
  mem.write(0x0004U, 2U, 0x2122U); // MOV.L R2,@R1
  mem.write(0x0006U, 2U, 0x0009U); // NOP

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);

  check(core.pc() == 0x0006U, "MOV.L Rm,@Rn should retire and advance PC");
  check(mem.read(0x0044U, 4U) == 0x0000007FU, "MOV.L Rm,@Rn should store to committed memory");
}



void test_sh2_bra_uses_delay_slot_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE001U); // MOV #1,R0
  mem.write(0x0002U, 2U, 0xA001U); // BRA +1 (target 0x0008)
  mem.write(0x0004U, 2U, 0x7001U); // ADD #1,R0 (delay slot)
  mem.write(0x0006U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x0008U, 2U, 0x7001U); // ADD #1,R0 (branch target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);

  check(core.reg(0) == 3U, "BRA should execute exactly one delay-slot instruction before branching");
  check(core.pc() == 0x000AU, "BRA delay-slot flow should land on branch target then advance");
}

void test_sh2_rts_uses_delay_slot_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0AU); // MOV #10,R15
  mem.write(0x0002U, 2U, 0xE001U); // MOV #1,R0
  mem.write(0x0004U, 2U, 0x000BU); // RTS
  mem.write(0x0006U, 2U, 0x7001U); // ADD #1,R0 (delay slot)
  mem.write(0x0008U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (RTS target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);
  core.step(arbiter, trace, 4);

  check(core.reg(0) == 3U, "RTS should execute exactly one delay-slot instruction before jumping to PR source");
  check(core.pc() == 0x000CU, "RTS delay-slot flow should land on branch target then advance");
}


void test_sh2_branch_in_delay_slot_uses_first_branch_target_policy() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE001U); // MOV #1,R0
  mem.write(0x0002U, 2U, 0xA002U); // BRA +2 (target 0x000A)
  mem.write(0x0004U, 2U, 0xA003U); // BRA +3 (delay-slot branch, ignored by first-branch-wins policy)
  mem.write(0x0006U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (first-branch target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);

  check(core.reg(0) == 2U,
        "branch in delay slot should not override first pending branch target in deterministic policy");
  check(core.pc() == 0x000CU,
        "first-branch-wins delay-slot policy should continue from the first branch target path");
}


void test_sh2_bra_with_movw_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0002U, 2U, 0xA002U); // BRA +2 (target 0x000A)
  mem.write(0x0004U, 2U, 0x6211U); // MOV.W @R1,R2 (delay-slot memory op)
  mem.write(0x0006U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (branch target)
  mem.write(0x0040U, 2U, 0xFF80U);

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);

  check(core.pc() == 0x000AU, "BRA with MOV.W delay-slot memory op should branch immediately after delay-slot commit");
  check(core.reg(2) == 0xFFFFFF80U, "MOV.W delay-slot read should still commit deterministic sign-extended value");

  core.step(arbiter, trace, 3);
  check(core.reg(0) == 1U, "BRA target instruction should execute after delay-slot memory commit");
}

void test_sh2_rts_with_movw_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0AU); // MOV #10,R15
  mem.write(0x0002U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0004U, 2U, 0x000BU); // RTS
  mem.write(0x0006U, 2U, 0x6211U); // MOV.W @R1,R2 (delay-slot memory op)
  mem.write(0x0008U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (RTS target)
  mem.write(0x0040U, 2U, 0x0001U);

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);

  check(core.pc() == 0x000AU, "RTS with MOV.W delay-slot memory op should branch immediately after delay-slot commit");
  check(core.reg(2) == 1U, "RTS delay-slot MOV.W read should deterministically update destination register");

  core.step(arbiter, trace, 4);
  check(core.reg(0) == 1U, "RTS target instruction should execute after delay-slot memory commit");
}


void test_sh2_bra_with_movl_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0002U, 2U, 0xA002U); // BRA +2 (target 0x000A)
  mem.write(0x0004U, 2U, 0x6212U); // MOV.L @R1,R2 (delay-slot memory op)
  mem.write(0x0006U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (branch target)
  mem.write(0x0040U, 4U, 0x12345678U);

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);

  check(core.pc() == 0x000AU, "BRA with MOV.L delay-slot memory op should branch immediately after delay-slot commit");
  check(core.reg(2) == 0x12345678U, "MOV.L delay-slot read should deterministically update destination register");

  core.step(arbiter, trace, 3);
  check(core.reg(0) == 1U, "BRA target instruction should execute after MOV.L delay-slot memory commit");
}

void test_sh2_rts_with_movl_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0AU); // MOV #10,R15
  mem.write(0x0002U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0004U, 2U, 0x000BU); // RTS
  mem.write(0x0006U, 2U, 0x6212U); // MOV.L @R1,R2 (delay-slot memory op)
  mem.write(0x0008U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (RTS target)
  mem.write(0x0040U, 4U, 0xABCDEF01U);

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);

  check(core.pc() == 0x000AU, "RTS with MOV.L delay-slot memory op should branch immediately after delay-slot commit");
  check(core.reg(2) == 0xABCDEF01U, "RTS delay-slot MOV.L read should deterministically update destination register");

  core.step(arbiter, trace, 4);
  check(core.reg(0) == 1U, "RTS target instruction should execute after MOV.L delay-slot memory commit");
}


void test_sh2_bra_with_movw_store_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE2FFU); // MOV #-1,R2
  mem.write(0x0002U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0004U, 2U, 0xA002U); // BRA +2 (target 0x000C)
  mem.write(0x0006U, 2U, 0x2121U); // MOV.W R2,@R1 (delay-slot memory op)
  mem.write(0x0008U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000CU, 2U, 0x7001U); // ADD #1,R0 (branch target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);

  check(core.pc() == 0x000CU,
        "BRA with MOV.W store delay-slot memory op should branch immediately after delay-slot commit");
  check(mem.read(0x0040U, 2U) == 0xFFFFU,
        "BRA delay-slot MOV.W store should deterministically commit low-halfword write before branch target executes");

  core.step(arbiter, trace, 4);
  check(core.reg(0) == 1U, "BRA target instruction should execute after MOV.W store delay-slot memory commit");
}

void test_sh2_rts_with_movw_store_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0AU); // MOV #10,R15
  mem.write(0x0002U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0004U, 2U, 0xE2AAU); // MOV #0xAA,R2
  mem.write(0x0006U, 2U, 0x000BU); // RTS
  mem.write(0x0008U, 2U, 0x2121U); // MOV.W R2,@R1 (delay-slot memory op)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (RTS target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);
  core.step(arbiter, trace, 4);

  check(core.pc() == 0x000AU,
        "RTS with MOV.W store delay-slot memory op should branch immediately after delay-slot commit");
  check(mem.read(0x0040U, 2U) == 0xFFAAU,
        "RTS delay-slot MOV.W store should deterministically commit low-halfword write before branch target executes");

  core.step(arbiter, trace, 5);
  check(core.reg(0) == 1U, "RTS target instruction should execute after MOV.W store delay-slot memory commit");
}

void test_sh2_bra_with_movl_store_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0002U, 2U, 0xE27FU); // MOV #0x7F,R2
  mem.write(0x0004U, 2U, 0xA002U); // BRA +2 (target 0x000C)
  mem.write(0x0006U, 2U, 0x2122U); // MOV.L R2,@R1 (delay-slot memory op)
  mem.write(0x0008U, 2U, 0x7001U); // ADD #1,R0 (should be skipped)
  mem.write(0x000CU, 2U, 0x7001U); // ADD #1,R0 (branch target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);

  check(core.pc() == 0x000CU,
        "BRA with MOV.L store delay-slot memory op should branch immediately after delay-slot commit");
  check(mem.read(0x0040U, 4U) == 0x0000007FU,
        "BRA delay-slot MOV.L store should deterministically commit word write before branch target executes");

  core.step(arbiter, trace, 4);
  check(core.reg(0) == 1U, "BRA target instruction should execute after MOV.L store delay-slot memory commit");
}

void test_sh2_rts_with_movl_store_delay_slot_applies_branch_after_memory_slot() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0CU); // MOV #12,R15
  mem.write(0x0002U, 2U, 0xE140U); // MOV #0x40,R1
  mem.write(0x0004U, 2U, 0xE255U); // MOV #0x55,R2
  mem.write(0x0006U, 2U, 0x000BU); // RTS
  mem.write(0x0008U, 2U, 0x2122U); // MOV.L R2,@R1 (delay-slot memory op)
  mem.write(0x000CU, 2U, 0x7001U); // ADD #1,R0 (RTS target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);
  core.step(arbiter, trace, 4);

  check(core.pc() == 0x000CU,
        "RTS with MOV.L store delay-slot memory op should branch immediately after delay-slot commit");
  check(mem.read(0x0040U, 4U) == 0x00000055U,
        "RTS delay-slot MOV.L store should deterministically commit word write before branch target executes");

  core.step(arbiter, trace, 5);
  check(core.reg(0) == 1U, "RTS target instruction should execute after MOV.L store delay-slot memory commit");
}


void test_scu_synthetic_source_overlapping_set_and_clear_same_batch_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000000U});
  const saturnis::bus::BusOp cpu0_set{0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000FF0U};
  const saturnis::bus::BusOp cpu1_clear{1, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x000000F0U};
  const auto committed = arbiter.commit_batch({cpu0_set, cpu1_clear});

  check(committed.size() == 2U, "same-batch overlap set/clear should commit both source ops");
  check(committed[0].op.cpu_id == 0 && committed[1].op.cpu_id == 1,
        "same-batch overlap set/clear should use deterministic arbitration ordering");

  const auto source_after = arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_after.value == 0x00000F00U,
        "same-batch overlap set/clear should deterministically leave expected overlapping bits");
}

void test_scu_synthetic_source_overlapping_set_and_clear_is_stable_across_runs() {
  std::uint32_t baseline = 0;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000000U});
    const saturnis::bus::BusOp cpu0_set{0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x5AU};
    const saturnis::bus::BusOp cpu1_clear{1, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x0AU};
    (void)arbiter.commit_batch({cpu0_set, cpu1_clear});

    const auto source_after = arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
    if (run == 0) {
      baseline = source_after.value;
    } else {
      check(source_after.value == baseline, "overlapping set/clear mixed-byte contention should be deterministic across runs");
    }
  }
}

void test_scu_overlapping_set_clear_respects_ims_masked_ist_view() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000F00U});
  const saturnis::bus::BusOp cpu0_set{0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000FF0U};
  const saturnis::bus::BusOp cpu1_clear{1, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000030U};
  (void)arbiter.commit_batch({cpu0_set, cpu1_clear});

  const auto ist = arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(ist.value == 0x000000C0U,
        "SCU IST should deterministically report overlap set/clear pending bits after IMS masking");
}

void test_commit_horizon_long_queue_drains_in_three_cycles() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 1U, 0, saturnis::bus::BusKind::Write, 0x7100U, 4, 0x11U},
                                            {1, 2U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000001U},
                                            {0, 6U, 2, saturnis::bus::BusKind::Write, 0x7104U, 4, 0x22U},
                                            {1, 9U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000001U},
                                            {0, 12U, 4, saturnis::bus::BusKind::Write, 0x7108U, 4, 0x33U}};

  arbiter.update_progress(0, 4U);
  arbiter.update_progress(1, 4U);
  const auto first = arbiter.commit_pending(pending);
  check(first.size() == 2U, "first cycle should drain only req_time values below initial horizon");

  arbiter.update_progress(0, 10U);
  arbiter.update_progress(1, 10U);
  const auto second = arbiter.commit_pending(pending);
  check(second.size() == 2U, "second cycle should drain middle req_time values once horizon advances");

  arbiter.update_progress(0, 20U);
  arbiter.update_progress(1, 20U);
  const auto third = arbiter.commit_pending(pending);
  check(third.size() == 1U, "third cycle should drain the final horizon-blocked op");
  check(pending.empty(), "long pending queue should be empty after three horizon cycles");
}

void test_commit_horizon_long_queue_preserves_remaining_order_each_cycle() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 2U, 0, saturnis::bus::BusKind::Write, 0x7200U, 4, 0xA1U},
                                            {1, 5U, 1, saturnis::bus::BusKind::Write, 0x7204U, 4, 0xA2U},
                                            {0, 8U, 2, saturnis::bus::BusKind::Write, 0x7208U, 4, 0xA3U},
                                            {1, 11U, 3, saturnis::bus::BusKind::Write, 0x720CU, 4, 0xA4U}};

  arbiter.update_progress(0, 6U);
  arbiter.update_progress(1, 6U);
  (void)arbiter.commit_pending(pending);
  check(pending.size() == 2U, "first long-queue cycle should leave two blocked ops");
  check(pending[0].phys_addr == 0x7208U && pending[1].phys_addr == 0x720CU,
        "remaining long-queue ops should keep deterministic relative order after first cycle");

  arbiter.update_progress(0, 20U);
  arbiter.update_progress(1, 20U);
  (void)arbiter.commit_pending(pending);
  check(pending.empty(), "second long-queue cycle should drain remaining ordered ops");
}

void test_sh2_bra_delay_slot_store_then_target_store_same_addr_resolves_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0002U, 2U, 0xE2AAU); // MOV #0xAA,R2
  mem.write(0x0004U, 2U, 0xA002U); // BRA +2 (target 0x000C)
  mem.write(0x0006U, 2U, 0x2121U); // MOV.W R2,@R1 (delay slot)
  mem.write(0x000CU, 2U, 0xE355U); // MOV #0x55,R3
  mem.write(0x000EU, 2U, 0x2131U); // MOV.W R3,@R1 (target path store)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  for (int i = 0; i < 7; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 2U) == 0x0055U,
        "BRA delay-slot store followed by target store to same addr should deterministically leave target value");
}

void test_sh2_rts_delay_slot_store_then_target_store_same_addr_resolves_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0EU); // MOV #14,R15
  mem.write(0x0002U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0004U, 2U, 0xE2AAU); // MOV #0xAA,R2
  mem.write(0x0006U, 2U, 0x000BU); // RTS
  mem.write(0x0008U, 2U, 0x2121U); // MOV.W R2,@R1 (delay slot)
  mem.write(0x000EU, 2U, 0xE355U); // MOV #0x55,R3 (target)
  mem.write(0x0010U, 2U, 0x2131U); // MOV.W R3,@R1 (target)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  for (int i = 0; i < 10; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 2U) == 0xFFAAU,
        "RTS same-address delay-slot/target store sequence should deterministically retain the modeled RTS-path value");
}


void test_scu_overlap_set_clear_two_batches_rotate_round_robin_winner() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp batch1_set{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x0000000FU};
  const saturnis::bus::BusOp batch1_clear{1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000003U};
  const auto first = arbiter.commit_batch({batch1_set, batch1_clear});
  check(first.size() == 2U, "first overlap batch should commit both ops");
  check(first[0].op.cpu_id == 0 && first[1].op.cpu_id == 1,
        "first overlap batch should use deterministic initial winner");

  const saturnis::bus::BusOp batch2_set{0, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000030U};
  const saturnis::bus::BusOp batch2_clear{1, 1U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000008U};
  const auto second = arbiter.commit_batch({batch2_set, batch2_clear});
  check(second.size() == 2U, "second overlap batch should commit both ops");
  check(second[0].op.cpu_id == 1 && second[1].op.cpu_id == 0,
        "second overlap batch should rotate round-robin winner deterministically");

  const auto source_after = arbiter.commit({0, 2U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source_after.value == 0x00000034U,
        "two-batch overlap set/clear sequence should deterministically resolve pending source bits");
}

void test_scu_overlap_set_clear_two_batches_are_stable_across_runs() {
  std::uint32_t baseline = 0;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    const saturnis::bus::BusOp set_a{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 2, 0x00F0U};
    const saturnis::bus::BusOp clear_a{1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x00F0U};
    (void)arbiter.commit_batch({set_a, clear_a});

    const saturnis::bus::BusOp set_b{0, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x1AU};
    const saturnis::bus::BusOp clear_b{1, 1U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 2, 0x000AU};
    (void)arbiter.commit_batch({set_b, clear_b});

    const auto source_after = arbiter.commit({0, 2U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
    if (run == 0) {
      baseline = source_after.value;
    } else {
      check(source_after.value == baseline,
            "two-batch overlap set/clear mixed-size sequence should be deterministic across repeated runs");
    }
  }
}

void test_commit_horizon_four_cycle_mixed_queue_drain_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 1U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000003U},
                                            {1, 3U, 1, saturnis::bus::BusKind::Write, 0x7300U, 4, 0x11111111U},
                                            {0, 6U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {1, 9U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000001U},
                                            {0, 12U, 4, saturnis::bus::BusKind::Write, 0x7304U, 4, 0x22222222U},
                                            {1, 15U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U}};

  arbiter.update_progress(0, 5U);
  arbiter.update_progress(1, 5U);
  const auto c1 = arbiter.commit_pending(pending);
  check(c1.size() == 2U, "cycle 1 should drain only the first two safe-horizon ops");

  arbiter.update_progress(0, 8U);
  arbiter.update_progress(1, 8U);
  const auto c2 = arbiter.commit_pending(pending);
  check(c2.size() == 1U, "cycle 2 should drain exactly the first queued MMIO read");
  check(c2[0].response.value == 0x00000003U,
        "cycle 2 MMIO read should observe deterministic value produced by earlier queued set op");

  arbiter.update_progress(0, 11U);
  arbiter.update_progress(1, 11U);
  const auto c3 = arbiter.commit_pending(pending);
  check(c3.size() == 1U, "cycle 3 should drain exactly the queued clear op");

  arbiter.update_progress(0, 20U);
  arbiter.update_progress(1, 20U);
  const auto c4 = arbiter.commit_pending(pending);
  check(c4.size() == 2U, "cycle 4 should drain the remaining RAM write and MMIO read");
  check((c4[0].op.phys_addr == 0x05FE00ACU && c4[0].response.value == 0x00000002U) ||
            (c4[1].op.phys_addr == 0x05FE00ACU && c4[1].response.value == 0x00000002U),
        "final queued MMIO read should observe deterministic post-clear source state");
  check(pending.empty(), "four-cycle mixed queue drain should leave no pending ops");
}

void test_commit_horizon_four_cycle_preserves_queue_order_each_step() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 2U, 0, saturnis::bus::BusKind::Write, 0x7400U, 4, 0x1U},
                                            {1, 4U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x1U},
                                            {0, 7U, 2, saturnis::bus::BusKind::Write, 0x7404U, 4, 0x2U},
                                            {1, 10U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x1U},
                                            {0, 13U, 4, saturnis::bus::BusKind::Write, 0x7408U, 4, 0x3U}};

  arbiter.update_progress(0, 5U);
  arbiter.update_progress(1, 5U);
  (void)arbiter.commit_pending(pending);
  check(pending.size() == 3U, "after first cycle, three long-queue ops should remain");
  check(pending[0].phys_addr == 0x7404U && pending[1].phys_addr == 0x05FE00B0U && pending[2].phys_addr == 0x7408U,
        "after first cycle, remaining long-queue ops should preserve deterministic relative order");

  arbiter.update_progress(0, 8U);
  arbiter.update_progress(1, 8U);
  (void)arbiter.commit_pending(pending);
  check(pending.size() == 2U, "after second cycle, two long-queue ops should remain");
  check(pending[0].phys_addr == 0x05FE00B0U && pending[1].phys_addr == 0x7408U,
        "after second cycle, remaining long-queue ops should still preserve order");

  arbiter.update_progress(0, 20U);
  arbiter.update_progress(1, 20U);
  (void)arbiter.commit_pending(pending);
  check(pending.empty(), "final cycle should drain all remaining ordered long-queue ops");
}

void test_sh2_bra_delay_slot_movw_then_target_movl_same_addr_overwrite_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0002U, 2U, 0xE201U); // MOV #1,R2
  mem.write(0x0004U, 2U, 0xA002U); // BRA +2 (target 0x000C)
  mem.write(0x0006U, 2U, 0x2121U); // MOV.W R2,@R1 (delay slot)
  mem.write(0x000CU, 2U, 0xE303U); // MOV #3,R3
  mem.write(0x000EU, 2U, 0x2132U); // MOV.L R3,@R1

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  for (int i = 0; i < 7; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 4U) == 0x00000003U,
        "BRA path MOV.W delay-slot store then MOV.L target store should deterministically leave target word value");
}

void test_sh2_bra_delay_slot_movl_then_target_movw_same_addr_overwrite_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0002U, 2U, 0xE2AAU); // MOV #0xAA,R2
  mem.write(0x0004U, 2U, 0xA002U); // BRA +2 (target 0x000C)
  mem.write(0x0006U, 2U, 0x2122U); // MOV.L R2,@R1 (delay slot)
  mem.write(0x000CU, 2U, 0xE355U); // MOV #0x55,R3
  mem.write(0x000EU, 2U, 0x2131U); // MOV.W R3,@R1

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  for (int i = 0; i < 7; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 4U) == 0xFFFF0055U,
        "BRA path MOV.L delay-slot store then MOV.W target store should deterministically preserve upper bits");
}

void test_sh2_rts_delay_slot_movw_then_target_movl_same_addr_overwrite_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0EU); // MOV #14,R15
  mem.write(0x0002U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0004U, 2U, 0xE201U); // MOV #1,R2
  mem.write(0x0006U, 2U, 0x000BU); // RTS
  mem.write(0x0008U, 2U, 0x2121U); // MOV.W R2,@R1 (delay slot)
  mem.write(0x000EU, 2U, 0xE303U); // MOV #3,R3
  mem.write(0x0010U, 2U, 0x2132U); // MOV.L R3,@R1

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  for (int i = 0; i < 11; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 4U) == 0x00000001U,
        "RTS mixed-width overwrite sequence should deterministically retain the modeled RTS-path value");
}

void test_sh2_rts_delay_slot_movl_then_target_movw_same_addr_overwrite_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0EU); // MOV #14,R15
  mem.write(0x0002U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0004U, 2U, 0xE2AAU); // MOV #0xAA,R2
  mem.write(0x0006U, 2U, 0x000BU); // RTS
  mem.write(0x0008U, 2U, 0x2122U); // MOV.L R2,@R1 (delay slot)
  mem.write(0x000EU, 2U, 0xE355U); // MOV #0x55,R3
  mem.write(0x0010U, 2U, 0x2131U); // MOV.W R3,@R1

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  for (int i = 0; i < 11; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 4U) == 0xFFFFFFAAU,
        "RTS inverse mixed-width overwrite sequence should deterministically retain the modeled RTS-path value");
}


void test_scu_overlap_two_batch_different_byte_lanes_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp b1_set{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0xAAU};
  const saturnis::bus::BusOp b1_clear{1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 1, 0x0FU};
  (void)arbiter.commit_batch({b1_set, b1_clear});

  const saturnis::bus::BusOp b2_set{0, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 1, 0xF0U};
  const saturnis::bus::BusOp b2_clear{1, 1U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0xA0U};
  (void)arbiter.commit_batch({b2_set, b2_clear});

  const auto source = arbiter.commit({0, 2U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(source.value == 0x00000AF0U,
        "two-batch SCU overlap operations on different byte lanes should deterministically resolve source bits");
}

void test_scu_overlap_staggered_req_time_matches_across_horizon_gating_runs() {
  std::uint32_t baseline = 0U;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    std::vector<saturnis::bus::BusOp> pending{{0, 2U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x0000003CU},
                                              {1, 7U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x0000000CU}};

    arbiter.update_progress(0, 5U);
    arbiter.update_progress(1, 5U);
    (void)arbiter.commit_pending(pending);
    check(pending.size() == 1U, "staggered req_time overlap test should hold later clear op behind first horizon");

    arbiter.update_progress(0, 10U);
    arbiter.update_progress(1, 10U);
    (void)arbiter.commit_pending(pending);
    check(pending.empty(), "staggered req_time overlap test should drain deterministically after horizon advance");

    const auto source = arbiter.commit({0, 11U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
    if (run == 0) {
      baseline = source.value;
    } else {
      check(source.value == baseline,
            "staggered req_time overlap behavior should remain deterministic across repeated runs");
    }
  }
}

void test_scu_overlap_ist_clear_while_masked_then_unmasked_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000030U});
  (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000020U});

  const auto masked_ist = arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(masked_ist.value == 0x10U, "masked IST view should suppress masked overlap bits");

  (void)arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000020U});
  (void)arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000000U});

  const auto unmasked_after_clear = arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(unmasked_after_clear.value == 0x10U,
        "IST clear while masked should deterministically persist clearing effect after unmasking");
}

void test_scu_overlap_write_log_order_and_payload_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit_batch({{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x12U},
                              {1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x02U}});

  const auto &writes = dev.writes();
  check(writes.size() == 2U, "overlap write-log test should capture both MMIO writes");
  check(writes[0].cpu == 0 && writes[0].addr == 0x05FE00ADU && writes[0].value == 0x12U,
        "overlap write-log first entry should preserve deterministic payload");
  check(writes[1].cpu == 1 && writes[1].addr == 0x05FE00B1U && writes[1].value == 0x02U,
        "overlap write-log second entry should preserve deterministic payload");
  check(writes[0].t < writes[1].t, "overlap write-log timestamps should be monotonic");
}

void test_commit_horizon_five_cycle_interleaved_mixed_ops_drain_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 1U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000007U},
                                            {1, 3U, 1, saturnis::bus::BusKind::Write, 0x7500U, 4, 0x11111111U},
                                            {0, 6U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {1, 9U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000001U},
                                            {0, 12U, 4, saturnis::bus::BusKind::Write, 0x7504U, 4, 0x22222222U},
                                            {1, 15U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {0, 18U, 6, saturnis::bus::BusKind::Write, 0x7508U, 4, 0x33333333U}};

  arbiter.update_progress(0, 4U);
  arbiter.update_progress(1, 4U);
  check(arbiter.commit_pending(pending).size() == 2U, "five-cycle drain cycle1 should commit two earliest ops");

  arbiter.update_progress(0, 7U);
  arbiter.update_progress(1, 7U);
  check(arbiter.commit_pending(pending).size() == 1U, "five-cycle drain cycle2 should commit queued MMIO read");

  arbiter.update_progress(0, 10U);
  arbiter.update_progress(1, 10U);
  check(arbiter.commit_pending(pending).size() == 1U, "five-cycle drain cycle3 should commit queued MMIO clear");

  arbiter.update_progress(0, 16U);
  arbiter.update_progress(1, 16U);
  check(arbiter.commit_pending(pending).size() == 2U, "five-cycle drain cycle4 should commit RAM write plus MMIO read");

  arbiter.update_progress(0, 25U);
  arbiter.update_progress(1, 25U);
  check(arbiter.commit_pending(pending).size() == 1U, "five-cycle drain cycle5 should commit final blocked RAM write");
  check(pending.empty(), "five-cycle interleaved mixed queue should be empty after deterministic draining");
}

void test_commit_horizon_mmio_read_value_respects_horizon_blocked_write_order() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000001U});

  std::vector<saturnis::bus::BusOp> pending{{0, 4U, 1, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {1, 9U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000006U},
                                            {0, 11U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U}};

  arbiter.update_progress(0, 6U);
  arbiter.update_progress(1, 6U);
  const auto first = arbiter.commit_pending(pending);
  check(first.size() == 1U && first[0].response.value == 0x00000001U,
        "queued MMIO read should observe pre-write state while later write remains horizon-blocked");

  arbiter.update_progress(0, 20U);
  arbiter.update_progress(1, 20U);
  const auto second = arbiter.commit_pending(pending);
  check(second.size() == 2U, "remaining queued write/read should drain after horizon advances");
  const auto final_source = arbiter.commit({0, 21U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(final_source.value == 0x00000007U,
        "post-write queued MMIO state should deterministically reflect queued write once horizon allows commit");
}

void test_commit_horizon_asymmetric_progress_updates_before_convergence_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 2U, 0, saturnis::bus::BusKind::Write, 0x7600U, 4, 0x1U},
                                            {1, 5U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x2U},
                                            {0, 8U, 2, saturnis::bus::BusKind::Write, 0x7604U, 4, 0x3U}};

  arbiter.update_progress(0, 10U);
  check(arbiter.commit_pending(pending).empty(),
        "asymmetric progress test should block commits until both CPU watermarks exist");

  arbiter.update_progress(1, 4U);
  const auto first = arbiter.commit_pending(pending);
  check(first.size() == 1U, "once second watermark appears, only req_time values below horizon should commit");

  arbiter.update_progress(1, 20U);
  const auto second = arbiter.commit_pending(pending);
  check(second.size() == 2U && pending.empty(),
        "asymmetric progress test should deterministically converge and drain remaining ops");
}


void test_sh2_bra_mixed_width_overwrite_with_negative_immediate_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0002U, 2U, 0xE2AAU); // MOV #0xAA,R2
  mem.write(0x0004U, 2U, 0xA002U); // BRA +2 (target 0x000C)
  mem.write(0x0006U, 2U, 0x2122U); // MOV.L R2,@R1 (delay slot)
  mem.write(0x000CU, 2U, 0xE3FFU); // MOV #-1,R3
  mem.write(0x000EU, 2U, 0x2131U); // MOV.W R3,@R1

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);
  for (int i = 0; i < 7; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 4U) == 0xFFFFFFFFU,
        "BRA mixed-width overwrite with negative immediate should deterministically preserve modeled upper bits");
}

void test_sh2_rts_mixed_width_overwrite_with_negative_immediate_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0EU); // MOV #14,R15
  mem.write(0x0002U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0004U, 2U, 0xE2AAU); // MOV #0xAA,R2
  mem.write(0x0006U, 2U, 0x000BU); // RTS
  mem.write(0x0008U, 2U, 0x2122U); // MOV.L R2,@R1 (delay slot)
  mem.write(0x000EU, 2U, 0xE3FFU); // MOV #-1,R3
  mem.write(0x0010U, 2U, 0x2131U); // MOV.W R3,@R1

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);
  for (int i = 0; i < 11; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(mem.read(0x0022U, 4U) == 0xFFFFFFAAU,
        "RTS mixed-width overwrite with negative immediate should deterministically preserve modeled upper bits");
}

void test_sh2_mmio_ram_same_address_overwrite_is_todo_and_current_subset_stays_deterministic() {
  // TODO: current SH-2 vertical subset lacks instruction support to materialize full MMIO base addresses in-register
  // using only implemented MOV #imm forms, so direct MMIO-vs-RAM same-address overwrite flow is not expressible yet.
  // Focused deterministic guard: assert the closest supported RAM-vs-RAM overwrite flow remains stable across runs.
  std::uint32_t baseline = 0U;
  for (int run = 0; run < 3; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    mem.write(0x0000U, 2U, 0xE122U); // MOV #0x22,R1
    mem.write(0x0002U, 2U, 0xE201U); // MOV #1,R2
    mem.write(0x0004U, 2U, 0xA002U); // BRA +2
    mem.write(0x0006U, 2U, 0x2121U); // MOV.W R2,@R1
    mem.write(0x000CU, 2U, 0xE355U); // MOV #0x55,R3
    mem.write(0x000EU, 2U, 0x2131U); // MOV.W R3,@R1

    saturnis::cpu::SH2Core core(0);
    core.reset(0U, 0x0001FFF0U);
    for (int i = 0; i < 7; ++i) {
      core.step(arbiter, trace, static_cast<std::uint64_t>(i));
    }

    const std::uint32_t value = mem.read(0x0022U, 2U);
    if (run == 0) {
      baseline = value;
    } else {
      check(value == baseline, "supported same-address overwrite baseline should remain deterministic while MMIO/RAM case is TODO");
    }
  }
}


void test_scu_overlap_byte_halfword_same_batch_has_lane_accurate_ist_visibility() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  const saturnis::bus::BusOp set_half{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 2, 0x12F0U};
  const saturnis::bus::BusOp clear_byte{1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x10U};
  (void)arbiter.commit_batch({set_half, clear_byte});

  const auto source = arbiter.commit({0, 1U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  const auto ist = arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(source.value == 0x000002F0U, "byte/halfword overlap should resolve source register lanes deterministically");
  check(ist.value == source.value, "lane-accurate overlap should produce matching unmasked IST visibility");
}

void test_scu_overlap_replayed_clears_are_idempotent_across_runs() {
  std::uint32_t baseline = 0U;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x0000003FU});
    (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x0000000FU});
    (void)arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x0000000FU});

    const auto source = arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
    if (run == 0) {
      baseline = source.value;
    } else {
      check(source.value == baseline,
            "replayed clear masks should be idempotent and deterministic across repeated runs");
    }
  }
}

void test_scu_overlap_alternating_set_clear_bursts_keep_ist_source_consistent() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000033U});
  (void)arbiter.commit({1, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000003U});
  (void)arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x000000C0U});
  (void)arbiter.commit({1, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000010U});

  const auto source = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  const auto ist = arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(source.value == 0x000000E0U, "alternating set/clear bursts should produce deterministic source state");
  check(ist.value == source.value, "unmasked IST view should stay consistent with source pending bits after bursts");
}

void test_scu_overlap_write_log_monotonic_deltas_are_stable_across_runs() {
  std::vector<std::uint64_t> baseline_deltas;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    (void)arbiter.commit_batch({{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x11U},
                                {1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x01U}});
    (void)arbiter.commit_batch({{0, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 2, 0x00F0U},
                                {1, 2U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 2, 0x000FU}});

    const auto &writes = dev.writes();
    check(writes.size() == 4U, "monotonic-delta overlap run should produce four deterministic MMIO writes");
    std::vector<std::uint64_t> deltas{writes[1].t - writes[0].t, writes[2].t - writes[1].t, writes[3].t - writes[2].t};
    check(writes[0].t < writes[1].t && writes[1].t < writes[2].t && writes[2].t < writes[3].t,
          "overlap write-log timestamps should be strictly monotonic");
    if (run == 0) {
      baseline_deltas = deltas;
    } else {
      check(deltas == baseline_deltas, "overlap write-log timestamp deltas should be stable across repeated runs");
    }
  }
}

void test_commit_horizon_six_cycle_alternating_mmio_pressure_drains_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 1U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x0000000FU},
                                            {1, 3U, 1, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {0, 6U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000003U},
                                            {1, 9U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {0, 12U, 4, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000030U},
                                            {1, 15U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {0, 18U, 6, saturnis::bus::BusKind::Write, 0x7700U, 4, 0x44U}};

  arbiter.update_progress(0, 4U); arbiter.update_progress(1, 4U); check(arbiter.commit_pending(pending).size() == 2U, "cycle1");
  arbiter.update_progress(0, 7U); arbiter.update_progress(1, 7U); check(arbiter.commit_pending(pending).size() == 1U, "cycle2");
  arbiter.update_progress(0, 10U); arbiter.update_progress(1, 10U); check(arbiter.commit_pending(pending).size() == 1U, "cycle3");
  arbiter.update_progress(0, 13U); arbiter.update_progress(1, 13U); check(arbiter.commit_pending(pending).size() == 1U, "cycle4");
  arbiter.update_progress(0, 16U); arbiter.update_progress(1, 16U); check(arbiter.commit_pending(pending).size() == 1U, "cycle5");
  arbiter.update_progress(0, 25U); arbiter.update_progress(1, 25U); check(arbiter.commit_pending(pending).size() == 1U, "cycle6");

  check(pending.empty(), "six-cycle alternating MMIO pressure queue should deterministically drain");
}

void test_commit_horizon_long_sequence_pins_each_queued_mmio_read_value() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 1U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000001U},
                                            {1, 2U, 1, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {0, 6U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000004U},
                                            {1, 7U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U},
                                            {0, 11U, 4, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000001U},
                                            {1, 12U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U}};

  arbiter.update_progress(0, 4U); arbiter.update_progress(1, 4U);
  const auto c1 = arbiter.commit_pending(pending);
  check(c1.size() == 2U && c1[1].response.value == 0x00000001U, "first queued MMIO read should see first queued write");

  arbiter.update_progress(0, 9U); arbiter.update_progress(1, 9U);
  const auto c2 = arbiter.commit_pending(pending);
  check(c2.size() == 2U && c2[1].response.value == 0x00000005U, "second queued MMIO read should see cumulative queued writes");

  arbiter.update_progress(0, 20U); arbiter.update_progress(1, 20U);
  const auto c3 = arbiter.commit_pending(pending);
  check(c3.size() == 2U, "third cycle should drain final queued write/read pair");
  const auto final_source = arbiter.commit({0, 21U, 6, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  check(final_source.value == 0x00000004U, "state after queued clear should be deterministic at end of sequence");
  check(pending.empty(), "long queued MMIO read/value sequence should fully drain");
}

void test_commit_horizon_alternating_asymmetric_updates_on_both_cpus_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0, 2U, 0, saturnis::bus::BusKind::Write, 0x7800U, 4, 0x1U},
                                            {1, 5U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x2U},
                                            {0, 8U, 2, saturnis::bus::BusKind::Write, 0x7804U, 4, 0x3U},
                                            {1, 11U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x1U}};

  arbiter.update_progress(0, 9U);
  check(arbiter.commit_pending(pending).empty(), "alternating-asymmetric test should block before both watermarks");

  arbiter.update_progress(1, 4U);
  (void)arbiter.commit_pending(pending);
  check(pending.size() == 3U, "phase2 should only release earliest op at low horizon");

  arbiter.update_progress(1, 10U);
  (void)arbiter.commit_pending(pending);
  check(pending.size() <= 1U,
        "phase3 should deterministically release additional committable ops after cpu1 lead update");

  arbiter.update_progress(0, 20U);
  arbiter.update_progress(1, 20U);
  (void)arbiter.commit_pending(pending);
  check(pending.empty(), "phase4 should deterministically converge and drain remaining ops");
}


void test_sh2_bra_mixed_width_both_negative_immediates_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0002U, 2U, 0xE2FFU); // MOV #-1,R2
  mem.write(0x0004U, 2U, 0xA002U); // BRA
  mem.write(0x0006U, 2U, 0x2122U); // MOV.L R2,@R1
  mem.write(0x000CU, 2U, 0xE3FFU); // MOV #-1,R3
  mem.write(0x000EU, 2U, 0x2131U); // MOV.W R3,@R1

  saturnis::cpu::SH2Core core(0); core.reset(0U, 0x0001FFF0U);
  for (int i=0;i<7;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "BRA both-negative mixed-width overwrite should be deterministic");
}

void test_sh2_rts_mixed_width_both_negative_immediates_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xEF0EU); // MOV #14,R15
  mem.write(0x0002U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0004U, 2U, 0xE2FFU); // MOV #-1,R2
  mem.write(0x0006U, 2U, 0x000BU); // RTS
  mem.write(0x0008U, 2U, 0x2122U); // MOV.L R2,@R1
  mem.write(0x000EU, 2U, 0xE3FFU); // MOV #-1,R3
  mem.write(0x0010U, 2U, 0x2131U); // MOV.W R3,@R1

  saturnis::cpu::SH2Core core(0); core.reset(0U, 0x0001FFF0U);
  for (int i=0;i<11;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "RTS both-negative mixed-width overwrite should be deterministic");
}

void test_sh2_same_addr_overwrite_with_intermediate_non_memory_instruction_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE122U); // MOV #0x22,R1
  mem.write(0x0002U, 2U, 0xE201U); // MOV #1,R2
  mem.write(0x0004U, 2U, 0xA003U); // BRA +3 (target 0x000E)
  mem.write(0x0006U, 2U, 0x2121U); // MOV.W R2,@R1 (delay)
  mem.write(0x000EU, 2U, 0x7001U); // ADD #1,R0 (non-memory)
  mem.write(0x0010U, 2U, 0xE355U); // MOV #0x55,R3
  mem.write(0x0012U, 2U, 0x2131U); // MOV.W R3,@R1

  saturnis::cpu::SH2Core core(0); core.reset(0U, 0x0001FFF0U);
  for (int i=0;i<9;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(mem.read(0x0022U,2U)==0x0055U, "same-address overwrite with intermediate non-memory op should be deterministic");
}


void test_scu_overlap_halfword_clear_byte_set_with_ims_mask_is_lane_accurate() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x0000FFFFU});
  const saturnis::bus::BusOp clear_half{0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 2, 0x00F0U};
  const saturnis::bus::BusOp set_byte{1, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x0AU};
  (void)arbiter.commit_batch({clear_half, set_byte});

  (void)arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000A00U});
  const auto source = arbiter.commit({0, 3U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  const auto ist = arbiter.commit({0, 4U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(source.value == 0x0000FF0FU, "halfword clear + byte set should preserve lane-accurate source state");
  check(ist.value == 0x0000F50FU, "IMS masking should suppress only the selected byte lane in IST view");
}

void test_scu_overlap_ist_clear_is_idempotent_with_interleaved_source_set() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000033U});
  (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000003U});
  (void)arbiter.commit({1, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000003U});
  (void)arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00A8U, 4, 0x00000003U});

  const auto source = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  const auto ist = arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(source.value == 0x00000030U, "interleaved source set plus repeated IST clear should be idempotent");
  check(ist.value == source.value, "IST view should match source after idempotent clear sequence");
}

void test_scu_overlap_alternating_mask_windows_keep_source_and_ist_consistent() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x000000F3U});
  (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x000000F0U});
  const auto masked = arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(masked.value == 0x00000003U, "masked IST window should expose only unmasked bits");

  (void)arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000001U});
  (void)arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 4, 0x00000000U});
  const auto source = arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  const auto unmasked = arbiter.commit({0, 6U, 6, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(source.value == 0x000000F2U, "source should reflect alternating mask/unmask burst state");
  check(unmasked.value == source.value, "unmasked IST should remain consistent with source register");
}

void test_scu_overlap_write_log_per_cpu_entry_counts_are_stable() {
  std::uint32_t baseline_cpu0 = 0U;
  std::uint32_t baseline_cpu1 = 0U;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    (void)arbiter.commit_batch({{0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x11U},
                                {1, 0U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x01U}});
    (void)arbiter.commit_batch({{0, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x22U},
                                {1, 2U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x02U}});

    std::uint32_t c0 = 0U, c1 = 0U;
    for (const auto &w : dev.writes()) {
      if (w.cpu == 0) ++c0;
      if (w.cpu == 1) ++c1;
    }
    if (run == 0) {
      baseline_cpu0 = c0;
      baseline_cpu1 = c1;
    } else {
      check(c0 == baseline_cpu0 && c1 == baseline_cpu1,
            "per-CPU overlap write-log entry counts should be deterministic across runs");
    }
  }
}

void test_commit_horizon_seven_cycle_alternating_ram_mmio_pressure_drain_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,1U,0,saturnis::bus::BusKind::Write,0x7900U,4,0x1U},
                                            {1,3U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x1U},
                                            {0,5U,2,saturnis::bus::BusKind::Write,0x7904U,4,0x2U},
                                            {1,7U,3,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,9U,4,saturnis::bus::BusKind::Write,0x7908U,4,0x3U},
                                            {1,11U,5,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U},
                                            {0,13U,6,saturnis::bus::BusKind::Write,0x790CU,4,0x4U},
                                            {1,15U,7,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U}};

  arbiter.update_progress(0,2U); arbiter.update_progress(1,2U); check(arbiter.commit_pending(pending).size()==1U, "c1");
  arbiter.update_progress(0,4U); arbiter.update_progress(1,4U); check(arbiter.commit_pending(pending).size()==1U, "c2");
  arbiter.update_progress(0,6U); arbiter.update_progress(1,6U); check(arbiter.commit_pending(pending).size()==1U, "c3");
  arbiter.update_progress(0,8U); arbiter.update_progress(1,8U); check(arbiter.commit_pending(pending).size()==1U, "c4");
  arbiter.update_progress(0,10U); arbiter.update_progress(1,10U); check(arbiter.commit_pending(pending).size()==1U, "c5");
  arbiter.update_progress(0,12U); arbiter.update_progress(1,12U); check(arbiter.commit_pending(pending).size()==1U, "c6");
  arbiter.update_progress(0,20U); arbiter.update_progress(1,20U); check(arbiter.commit_pending(pending).size()==2U, "c7");
  check(pending.empty(), "seven-cycle alternating RAM/MMIO pressure queue should deterministically drain");
}

void test_commit_horizon_four_queued_mmio_reads_have_pinned_values() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,1U,0,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x1U},
                                            {1,2U,1,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,4U,2,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x2U},
                                            {1,5U,3,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,7U,4,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U},
                                            {1,8U,5,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,10U,6,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x8U},
                                            {1,11U,7,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U}};

  auto read_value_from_commits = [](const std::vector<saturnis::bus::CommitResult> &commits) -> std::uint32_t {
    for (const auto &commit : commits) {
      if (commit.op.kind == saturnis::bus::BusKind::MmioRead) {
        return commit.response.value;
      }
    }
    return 0xFFFFFFFFU;
  };

  arbiter.update_progress(0,3U); arbiter.update_progress(1,3U);
  const auto c1 = arbiter.commit_pending(pending);
  check(c1.size()==2U && read_value_from_commits(c1)==0x1U, "read1 pinned value");
  arbiter.update_progress(0,6U); arbiter.update_progress(1,6U);
  const auto c2 = arbiter.commit_pending(pending);
  check(c2.size()==2U && read_value_from_commits(c2)==0x3U, "read2 pinned value");
  arbiter.update_progress(0,9U); arbiter.update_progress(1,9U);
  const auto c3 = arbiter.commit_pending(pending);
  check(c3.size()==2U && read_value_from_commits(c3)==0x3U, "read3 pinned value");
  arbiter.update_progress(0,20U); arbiter.update_progress(1,20U);
  const auto c4 = arbiter.commit_pending(pending);
  check(c4.size()==2U && read_value_from_commits(c4)==0xAU, "read4 pinned value");
  check(pending.empty(), "four queued MMIO reads sequence should fully drain");
}

void test_commit_horizon_progress_alternation_reverses_midway_before_convergence() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,2U,0,saturnis::bus::BusKind::Write,0x7A00U,4,0x1U},
                                            {1,4U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x2U},
                                            {0,6U,2,saturnis::bus::BusKind::Write,0x7A04U,4,0x3U},
                                            {1,8U,3,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U}};

  arbiter.update_progress(0,7U);
  check(arbiter.commit_pending(pending).empty(), "midway-reversal phase1 blocked before both watermarks");
  arbiter.update_progress(1,5U);
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "midway-reversal phase2 should release early committable ops");
  arbiter.update_progress(0,6U); // reverse (no effect since monotonic), ensure deterministic no regression
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "midway-reversal phase3 should not regress horizon");
  arbiter.update_progress(1,20U);
  arbiter.update_progress(0,20U);
  (void)arbiter.commit_pending(pending);
  check(pending.empty(), "midway-reversal phase4 should converge and drain deterministically");
}

void test_sh2_bra_both_negative_mixed_width_with_followup_target_arithmetic_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xE122U); // MOV #0x22,R1
  mem.write(0x0002U,2U,0xE2FFU); // MOV #-1,R2
  mem.write(0x0004U,2U,0xA003U); // BRA +3 target 0x000E
  mem.write(0x0006U,2U,0x2122U); // MOV.L R2,@R1
  mem.write(0x000EU,2U,0x7001U); // ADD #1,R0
  mem.write(0x0010U,2U,0xE3FFU); // MOV #-1,R3
  mem.write(0x0012U,2U,0x2131U); // MOV.W R3,@R1
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<10;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(core.reg(0)==1U, "BRA follow-up arithmetic should execute deterministically at target path");
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "BRA both-negative mixed-width overwrite with follow-up arithmetic should be deterministic");
}

void test_sh2_rts_both_negative_mixed_width_with_followup_target_arithmetic_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xEF10U); // MOV #16,R15
  mem.write(0x0002U,2U,0xE122U); // MOV #0x22,R1
  mem.write(0x0004U,2U,0xE2FFU); // MOV #-1,R2
  mem.write(0x0006U,2U,0x000BU); // RTS
  mem.write(0x0008U,2U,0x2122U); // MOV.L R2,@R1
  mem.write(0x0010U,2U,0x7001U); // ADD #1,R0
  mem.write(0x0012U,2U,0xE3FFU); // MOV #-1,R3
  mem.write(0x0014U,2U,0x2131U); // MOV.W R3,@R1
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<13;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(core.reg(0)==1U, "RTS follow-up arithmetic should execute deterministically at target path");
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "RTS both-negative mixed-width overwrite with follow-up arithmetic should be deterministic");
}

void test_sh2_same_addr_overwrite_with_two_intermediate_non_memory_instructions_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xE122U); // MOV #0x22,R1
  mem.write(0x0002U,2U,0xE201U); // MOV #1,R2
  mem.write(0x0004U,2U,0xA004U); // BRA +4 target 0x0010
  mem.write(0x0006U,2U,0x2121U); // delay-store
  mem.write(0x0010U,2U,0x7001U); // ADD #1,R0
  mem.write(0x0012U,2U,0x7001U); // ADD #1,R0
  mem.write(0x0014U,2U,0xE355U); // MOV #0x55,R3
  mem.write(0x0016U,2U,0x2131U); // target-store
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<12;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(core.reg(0)==2U, "two intermediate non-memory instructions should deterministically execute before target store");
  check(mem.read(0x0022U,2U)==0x0055U, "same-address overwrite with two intermediate instructions should be deterministic");
}


void test_scu_overlap_opposite_lane_halfword_set_byte_clear_with_ims_mask_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0,0U,0,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x00000000U});
  (void)arbiter.commit({0,1U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,2,0xAA55U});
  (void)arbiter.commit({1,2U,2,saturnis::bus::BusKind::MmioWrite,0x05FE00B1U,1,0xFFU});
  (void)arbiter.commit({0,3U,3,saturnis::bus::BusKind::MmioWrite,0x05FE00A0U,4,0x00000050U});

  const auto source = arbiter.commit({0,4U,4,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U});
  const auto ist = arbiter.commit({1,5U,5,saturnis::bus::BusKind::MmioRead,0x05FE00A4U,4,0U});
  check(source.value==0x00000055U, "opposite-lane halfword set + byte clear should be lane accurate");
  check(ist.value==0x00000005U, "IMS mask should preserve unmasked opposite-lane halfword result");
}

void test_scu_overlap_three_batch_alternating_set_clear_has_stable_final_source() {
  std::uint32_t baseline = 0U;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    (void)arbiter.commit_batch({{0,0U,0,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x0000000FU},
                                {1,0U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x00000003U}});
    (void)arbiter.commit_batch({{1,2U,2,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x00000030U},
                                {0,2U,3,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x00000008U}});
    (void)arbiter.commit_batch({{0,4U,4,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x00000080U},
                                {1,4U,5,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x00000020U}});

    const auto source = arbiter.commit({0,5U,6,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U});
    if (run == 0) {
      baseline = source.value;
    } else {
      check(source.value==baseline, "three-batch alternating overlap should converge to stable final source value");
    }
  }
}

void test_scu_overlap_replayed_ist_clear_preserves_ist_source_agreement() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0,0U,0,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x000000F3U});
  (void)arbiter.commit({1,1U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00A8U,4,0x00000003U});
  (void)arbiter.commit({1,2U,2,saturnis::bus::BusKind::MmioWrite,0x05FE00A8U,4,0x00000003U});
  (void)arbiter.commit({0,3U,3,saturnis::bus::BusKind::MmioWrite,0x05FE00A8U,4,0x00000030U});

  const auto source = arbiter.commit({0,4U,4,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U});
  const auto ist = arbiter.commit({1,5U,5,saturnis::bus::BusKind::MmioRead,0x05FE00A4U,4,0U});
  check(source.value==0x000000C0U, "replayed IST clear writes should keep source deterministic");
  check(ist.value==source.value, "IST/source agreement must hold after replayed IST clear writes");
}

void test_scu_overlap_write_log_per_cpu_value_histograms_are_stable() {
  std::size_t base0 = 0U;
  std::size_t base1 = 0U;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    (void)arbiter.commit_batch({{0,0U,0,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x11U},
                                {1,0U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x01U}});
    (void)arbiter.commit_batch({{0,2U,2,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x22U},
                                {1,2U,3,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x02U}});

    std::size_t c0 = 0U;
    std::size_t c1 = 0U;
    for (const auto &w : dev.writes()) {
      if (w.cpu == 0 && (w.value == 0x11U || w.value == 0x22U)) ++c0;
      if (w.cpu == 1 && (w.value == 0x01U || w.value == 0x02U)) ++c1;
    }

    if (run == 0) {
      base0 = c0;
      base1 = c1;
    } else {
      check(c0 == base0 && c1 == base1, "per-CPU write-log value histograms should be stable across runs");
    }
  }
}

void test_commit_horizon_eight_cycle_mixed_ram_mmio_drain_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,1U,0,saturnis::bus::BusKind::Write,0x7B00U,4,0x1U},
                                            {1,3U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x1U},
                                            {0,5U,2,saturnis::bus::BusKind::Write,0x7B04U,4,0x2U},
                                            {1,7U,3,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,9U,4,saturnis::bus::BusKind::Write,0x7B08U,4,0x3U},
                                            {1,11U,5,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U},
                                            {0,13U,6,saturnis::bus::BusKind::Write,0x7B0CU,4,0x4U},
                                            {1,15U,7,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,17U,8,saturnis::bus::BusKind::Write,0x7B10U,4,0x5U}};

  for (std::uint32_t t = 2U; t <= 16U; t += 2U) {
    arbiter.update_progress(0,t); arbiter.update_progress(1,t);
    check(arbiter.commit_pending(pending).size()==1U, "eight-cycle drain should release one op per cycle before convergence");
  }
  arbiter.update_progress(0,30U); arbiter.update_progress(1,30U);
  check(arbiter.commit_pending(pending).size()==1U, "eight-cycle drain final op should release at convergence");
  check(pending.empty(), "eight-cycle mixed RAM/MMIO queue should deterministically drain");
}

void test_commit_horizon_five_queued_mmio_reads_have_pinned_values() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,1U,0,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x1U},
                                            {1,2U,1,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,4U,2,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x2U},
                                            {1,5U,3,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,7U,4,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U},
                                            {1,8U,5,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,10U,6,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x8U},
                                            {1,11U,7,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,13U,8,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x8U},
                                            {1,14U,9,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U}};

  auto read_value = [](const std::vector<saturnis::bus::CommitResult> &commits) {
    for (const auto &c : commits) if (c.op.kind == saturnis::bus::BusKind::MmioRead) return c.response.value;
    return 0xFFFFFFFFU;
  };

  arbiter.update_progress(0,3U); arbiter.update_progress(1,3U); check(read_value(arbiter.commit_pending(pending))==0x1U, "read1 pinned value");
  arbiter.update_progress(0,6U); arbiter.update_progress(1,6U); check(read_value(arbiter.commit_pending(pending))==0x3U, "read2 pinned value");
  arbiter.update_progress(0,9U); arbiter.update_progress(1,9U); check(read_value(arbiter.commit_pending(pending))==0x3U, "read3 pinned value");
  arbiter.update_progress(0,12U); arbiter.update_progress(1,12U); check(read_value(arbiter.commit_pending(pending))==0xAU, "read4 pinned value");
  arbiter.update_progress(0,20U); arbiter.update_progress(1,20U); check(read_value(arbiter.commit_pending(pending))==0xAU, "read5 pinned value");
  check(pending.empty(), "five queued MMIO reads sequence should fully drain");
}

void test_commit_horizon_progress_reverses_twice_before_convergence() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,2U,0,saturnis::bus::BusKind::Write,0x7C00U,4,0x1U},
                                            {1,4U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x2U},
                                            {0,6U,2,saturnis::bus::BusKind::Write,0x7C04U,4,0x3U},
                                            {1,8U,3,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U}};

  arbiter.update_progress(0,7U);
  check(arbiter.commit_pending(pending).empty(), "reverse-twice phase1 should block before both watermarks");
  arbiter.update_progress(1,5U);
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "reverse-twice phase2 should release early ops");
  arbiter.update_progress(0,6U);
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "reverse-twice phase3 should not regress after first reversal");
  arbiter.update_progress(1,4U);
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "reverse-twice phase4 should not regress after second reversal");
  arbiter.update_progress(0,30U); arbiter.update_progress(1,30U);
  (void)arbiter.commit_pending(pending);
  check(pending.empty(), "reverse-twice phase5 should converge deterministically");
}

void test_sh2_bra_both_negative_mixed_width_with_dual_target_arithmetic_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xE122U);
  mem.write(0x0002U,2U,0xE2FFU);
  mem.write(0x0004U,2U,0xA003U);
  mem.write(0x0006U,2U,0x2122U);
  mem.write(0x000EU,2U,0x7001U);
  mem.write(0x0010U,2U,0x7001U);
  mem.write(0x0012U,2U,0xE3FFU);
  mem.write(0x0014U,2U,0x2131U);
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<12;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(core.reg(0)==2U, "BRA dual target arithmetic should execute deterministically");
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "BRA both-negative mixed-width overwrite with dual arithmetic should be deterministic");
}

void test_sh2_rts_both_negative_mixed_width_with_dual_target_arithmetic_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xEF10U);
  mem.write(0x0002U,2U,0xE122U);
  mem.write(0x0004U,2U,0xE2FFU);
  mem.write(0x0006U,2U,0x000BU);
  mem.write(0x0008U,2U,0x2122U);
  mem.write(0x0010U,2U,0x7001U);
  mem.write(0x0012U,2U,0x7001U);
  mem.write(0x0014U,2U,0xE3FFU);
  mem.write(0x0016U,2U,0x2131U);
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<14;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(core.reg(0)==2U, "RTS dual target arithmetic should execute deterministically");
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "RTS both-negative mixed-width overwrite with dual arithmetic should be deterministic");
}

void test_sh2_same_addr_overwrite_with_three_intermediate_non_memory_instructions_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xE122U);
  mem.write(0x0002U,2U,0xE201U);
  mem.write(0x0004U,2U,0xA005U);
  mem.write(0x0006U,2U,0x2121U);
  mem.write(0x0012U,2U,0x7001U);
  mem.write(0x0014U,2U,0x7001U);
  mem.write(0x0016U,2U,0x7001U);
  mem.write(0x0018U,2U,0xE355U);
  mem.write(0x001AU,2U,0x2131U);
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<14;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(core.reg(0)==3U, "three intermediate non-memory instructions should deterministically execute before target store");
  check(mem.read(0x0022U,2U)==0x0055U, "same-address overwrite with three intermediate instructions should be deterministic");
}


void test_scu_overlap_non_adjacent_byte_writes_same_batch_are_lane_accurate() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x000000FFU});
  const saturnis::bus::BusOp set_upper_byte{0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0xA0U};
  const saturnis::bus::BusOp clear_lower_byte{1, 1U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 1, 0x0FU};
  (void)arbiter.commit_batch({set_upper_byte, clear_lower_byte});

  const auto source = arbiter.commit({0, 2U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  const auto ist = arbiter.commit({0, 3U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(source.value == 0x0000A0F0U, "non-adjacent byte lanes across set/clear registers should resolve deterministically in one batch");
  check(ist.value == source.value, "unmasked IST should preserve non-adjacent lane overlap outcome");
}

void test_scu_overlap_source_clear_writes_are_idempotent_across_five_runs() {
  std::uint32_t baseline = 0U;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000033U});
    (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000003U});
    (void)arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 4, 0x00000003U});

    const auto source = arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
    if (run == 0) {
      baseline = source.value;
    } else {
      check(source.value == baseline, "repeated source-clear writes should remain idempotent across five runs");
    }
  }
}

void test_scu_overlap_ist_mask_retention_with_alternating_halfword_ims_writes() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  (void)arbiter.commit({0, 0U, 0, saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x0000003CU});
  (void)arbiter.commit({0, 1U, 1, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 2, 0x000CU});
  const auto masked = arbiter.commit({0, 2U, 2, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(masked.value == 0x00000030U, "halfword IMS mask write should retain only unmasked IST bits");

  (void)arbiter.commit({0, 3U, 3, saturnis::bus::BusKind::MmioWrite, 0x05FE00A0U, 2, 0x0000U});
  const auto source = arbiter.commit({0, 4U, 4, saturnis::bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});
  const auto unmasked = arbiter.commit({0, 5U, 5, saturnis::bus::BusKind::MmioRead, 0x05FE00A4U, 4, 0U});
  check(source.value == 0x0000003CU, "source state should remain stable across alternating halfword IMS writes");
  check(unmasked.value == source.value, "unmasked IST should restore full retained source visibility");
}

void test_scu_overlap_write_log_address_histograms_are_stable_across_bursts() {
  std::vector<std::uint32_t> baseline;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    for (int burst = 0; burst < 3; ++burst) {
      (void)arbiter.commit_batch({{0, static_cast<std::uint64_t>(burst * 4 + 0), static_cast<std::uint64_t>(burst * 4 + 0), saturnis::bus::BusKind::MmioWrite, 0x05FE00ACU, 1, 0x11U},
                                  {1, static_cast<std::uint64_t>(burst * 4 + 1), static_cast<std::uint64_t>(burst * 4 + 1), saturnis::bus::BusKind::MmioWrite, 0x05FE00B0U, 1, 0x01U},
                                  {0, static_cast<std::uint64_t>(burst * 4 + 2), static_cast<std::uint64_t>(burst * 4 + 2), saturnis::bus::BusKind::MmioWrite, 0x05FE00ADU, 1, 0x22U},
                                  {1, static_cast<std::uint64_t>(burst * 4 + 3), static_cast<std::uint64_t>(burst * 4 + 3), saturnis::bus::BusKind::MmioWrite, 0x05FE00B1U, 1, 0x02U}});
    }

    std::uint32_t ac = 0U, ad = 0U, b0 = 0U, b1 = 0U;
    for (const auto &w : dev.writes()) {
      if (w.addr == 0x05FE00ACU) ++ac;
      if (w.addr == 0x05FE00ADU) ++ad;
      if (w.addr == 0x05FE00B0U) ++b0;
      if (w.addr == 0x05FE00B1U) ++b1;
    }
    const std::vector<std::uint32_t> hist{ac, ad, b0, b1};
    if (run == 0) {
      baseline = hist;
    } else {
      check(hist == baseline, "SCU overlap write-log address histograms should be stable across repeated bursts");
    }
  }
}

void test_sh2_bra_both_negative_overwrite_with_target_register_copy_before_store_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xE122U);
  mem.write(0x0002U,2U,0xE2FFU);
  mem.write(0x0004U,2U,0xA003U);
  mem.write(0x0006U,2U,0x2122U);
  mem.write(0x000EU,2U,0xE3FFU);
  mem.write(0x0010U,2U,0x6233U);
  mem.write(0x0012U,2U,0x2131U);
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<11;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "BRA both-negative overwrite with target register copy before store should be deterministic");
}

void test_sh2_rts_both_negative_overwrite_with_target_register_copy_before_store_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xEF0EU);
  mem.write(0x0002U,2U,0xE122U);
  mem.write(0x0004U,2U,0xE2FFU);
  mem.write(0x0006U,2U,0x000BU);
  mem.write(0x0008U,2U,0x2122U);
  mem.write(0x000EU,2U,0xE3FFU);
  mem.write(0x0010U,2U,0x6233U);
  mem.write(0x0012U,2U,0x2131U);
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<12;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(mem.read(0x0022U,4U)==0xFFFFFFFFU, "RTS both-negative overwrite with target register copy before store should be deterministic");
}

void test_sh2_same_addr_overwrite_with_four_intermediate_non_memory_instructions_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);
  mem.write(0x0000U,2U,0xE122U);
  mem.write(0x0002U,2U,0xE201U);
  mem.write(0x0004U,2U,0xA006U);
  mem.write(0x0006U,2U,0x2121U);
  mem.write(0x0014U,2U,0x7001U);
  mem.write(0x0016U,2U,0x7001U);
  mem.write(0x0018U,2U,0x7001U);
  mem.write(0x001AU,2U,0x7001U);
  mem.write(0x001CU,2U,0xE355U);
  mem.write(0x001EU,2U,0x2131U);
  saturnis::cpu::SH2Core core(0); core.reset(0U,0x0001FFF0U);
  for (int i=0;i<16;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  check(core.reg(0)==4U, "four intermediate non-memory instructions should execute before target store");
  check(mem.read(0x0022U,2U)==0x0055U, "same-address overwrite with four intermediate instructions should be deterministic");
}

void test_commit_horizon_nine_cycle_mixed_ram_mmio_drain_is_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,2U,0,saturnis::bus::BusKind::Write,0x7B00U,4,0x1U},
                                            {1,3U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x1U},
                                            {0,5U,2,saturnis::bus::BusKind::Write,0x7B04U,4,0x2U},
                                            {1,7U,3,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,9U,4,saturnis::bus::BusKind::Write,0x7B08U,4,0x3U},
                                            {1,11U,5,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U},
                                            {0,13U,6,saturnis::bus::BusKind::Write,0x7B0CU,4,0x4U},
                                            {1,15U,7,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                            {0,17U,8,saturnis::bus::BusKind::Write,0x7B10U,4,0x5U},
                                            {1,19U,9,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U}};

  std::size_t committed = 0U;
  std::size_t prev_pending = pending.size();
  for (std::uint32_t t = 2U; t <= 18U; t += 2U) {
    arbiter.update_progress(0,t); arbiter.update_progress(1,t);
    const auto commits = arbiter.commit_pending(pending);
    committed += commits.size();
    check(pending.size() <= prev_pending, "nine-cycle drain should never increase pending queue size");
    prev_pending = pending.size();
  }
  arbiter.update_progress(0,30U); arbiter.update_progress(1,30U);
  committed += arbiter.commit_pending(pending).size();
  check(committed == 10U, "nine-cycle mixed queue should deterministically commit all queued ops");
  check(pending.empty(), "nine-cycle mixed RAM/MMIO queue should deterministically drain");
}

void test_commit_horizon_six_queued_mmio_reads_have_pinned_values() {
  std::vector<std::uint32_t> baseline_reads;
  for (int run = 0; run < 5; ++run) {
    saturnis::core::TraceLog trace;
    saturnis::mem::CommittedMemory mem;
    saturnis::dev::DeviceHub dev;
    saturnis::bus::BusArbiter arbiter(mem, dev, trace);

    std::vector<saturnis::bus::BusOp> pending{{0,1U,0,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x1U},
                                              {1,2U,1,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                              {0,4U,2,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x2U},
                                              {1,5U,3,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                              {0,7U,4,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U},
                                              {1,8U,5,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                              {0,10U,6,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x8U},
                                              {1,11U,7,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                              {0,13U,8,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x8U},
                                              {1,14U,9,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U},
                                              {0,16U,10,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x4U},
                                              {1,17U,11,saturnis::bus::BusKind::MmioRead,0x05FE00ACU,4,0U}};

    auto read_value = [](const std::vector<saturnis::bus::CommitResult> &commits) {
      for (const auto &c : commits) if (c.op.kind == saturnis::bus::BusKind::MmioRead) return c.response.value;
      return 0xFFFFFFFFU;
    };

    std::vector<std::uint32_t> run_reads;
    arbiter.update_progress(0,3U); arbiter.update_progress(1,3U); run_reads.push_back(read_value(arbiter.commit_pending(pending)));
    arbiter.update_progress(0,6U); arbiter.update_progress(1,6U); run_reads.push_back(read_value(arbiter.commit_pending(pending)));
    arbiter.update_progress(0,9U); arbiter.update_progress(1,9U); run_reads.push_back(read_value(arbiter.commit_pending(pending)));
    arbiter.update_progress(0,12U); arbiter.update_progress(1,12U); run_reads.push_back(read_value(arbiter.commit_pending(pending)));
    arbiter.update_progress(0,15U); arbiter.update_progress(1,15U); run_reads.push_back(read_value(arbiter.commit_pending(pending)));
    arbiter.update_progress(0,20U); arbiter.update_progress(1,20U); run_reads.push_back(read_value(arbiter.commit_pending(pending)));
    check(pending.empty(), "six queued MMIO reads sequence should fully drain");

    if (run == 0) {
      baseline_reads = run_reads;
    } else {
      check(run_reads == baseline_reads, "six queued MMIO reads should keep pinned values across repeated runs");
    }
  }
}


void test_commit_horizon_progress_reverses_on_both_cpus_before_convergence() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<saturnis::bus::BusOp> pending{{0,2U,0,saturnis::bus::BusKind::Write,0x7C00U,4,0x1U},
                                            {1,4U,1,saturnis::bus::BusKind::MmioWrite,0x05FE00ACU,4,0x2U},
                                            {0,6U,2,saturnis::bus::BusKind::Write,0x7C04U,4,0x3U},
                                            {1,8U,3,saturnis::bus::BusKind::MmioWrite,0x05FE00B0U,4,0x1U}};

  arbiter.update_progress(0,7U); arbiter.update_progress(1,5U);
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "both-cpu reversal phase1 should release the first two ops");
  arbiter.update_progress(0,6U); arbiter.update_progress(1,4U);
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "both-cpu reversal phase2 should hold after first simultaneous reversal");
  arbiter.update_progress(0,5U); arbiter.update_progress(1,3U);
  (void)arbiter.commit_pending(pending);
  check(pending.size()==2U, "both-cpu reversal phase3 should hold after second simultaneous reversal");
  arbiter.update_progress(0,30U); arbiter.update_progress(1,30U);
  (void)arbiter.commit_pending(pending);
  check(pending.empty(), "both-cpu reversal phase4 should converge deterministically");
}

void test_sh2_cmp_eq_and_tst_update_tbit_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE105U); // MOV #5,R1
  mem.write(0x0002U, 2U, 0xE205U); // MOV #5,R2
  mem.write(0x0004U, 2U, 0x3210U); // CMP/EQ R1,R2
  mem.write(0x0006U, 2U, 0x0329U); // MOVT R3
  mem.write(0x0008U, 2U, 0xE00FU); // MOV #15,R0
  mem.write(0x000AU, 2U, 0x880FU); // CMP/EQ #15,R0
  mem.write(0x000CU, 2U, 0x0429U); // MOVT R4
  mem.write(0x000EU, 2U, 0xE101U); // MOV #1,R1
  mem.write(0x0010U, 2U, 0x2218U); // TST R1,R2
  mem.write(0x0012U, 2U, 0x0529U); // MOVT R5

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);
  for (int i = 0; i < 10; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(core.reg(3) == 1U, "CMP/EQ Rm,Rn should set T when operands are equal");
  check(core.reg(4) == 1U, "CMP/EQ #imm,R0 should set T on equal immediate compare");
  check(core.reg(5) == 0U, "TST Rm,Rn should clear T when bitwise-and result is non-zero");
  check(core.sr() == 0x000000F0U, "final SR should reflect T cleared after non-zero TST");

  const auto json = trace.to_jsonl();
  check(json.find("\"sr\":241") != std::string::npos,
        "trace should capture T=1 states produced by compare instructions");
  check(json.find("\"sr\":240") != std::string::npos,
        "trace should capture T=0 state after TST clears T");
}

void test_sh2_bt_bf_and_s_forms_follow_deterministic_branch_rules() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0x0018U); // SETT
  mem.write(0x0002U, 2U, 0x8901U); // BT +1 -> 0x0008
  mem.write(0x0004U, 2U, 0x7001U); // ADD #1,R0 (skipped)
  mem.write(0x0006U, 2U, 0x7001U); // ADD #1,R0 (skipped)
  mem.write(0x0008U, 2U, 0x0008U); // CLRT
  mem.write(0x000AU, 2U, 0x8B01U); // BF +1 -> 0x0010
  mem.write(0x000CU, 2U, 0x7001U); // ADD #1,R0 (skipped)
  mem.write(0x000EU, 2U, 0x7001U); // ADD #1,R0 (skipped)
  mem.write(0x0010U, 2U, 0x0018U); // SETT
  mem.write(0x0012U, 2U, 0x8D01U); // BT/S +1 -> delay slot at 0x0014, then 0x0018
  mem.write(0x0014U, 2U, 0x7001U); // ADD #1,R0 (delay slot, must execute)
  mem.write(0x0016U, 2U, 0x7001U); // ADD #1,R0 (skipped by taken branch)
  mem.write(0x0018U, 2U, 0x0008U); // CLRT
  mem.write(0x001AU, 2U, 0x8F01U); // BF/S +1 -> delay slot at 0x001C, then 0x0020
  mem.write(0x001CU, 2U, 0x7001U); // ADD #1,R0 (delay slot, must execute)
  mem.write(0x001EU, 2U, 0x7001U); // ADD #1,R0 (skipped)
  mem.write(0x0020U, 2U, 0x0009U); // NOP

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);
  for (int i = 0; i < 14; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(core.reg(0) == 2U, "BT/BF and BT/S/BF/S matrix should execute exactly the two delay-slot adds");
  const auto json = trace.to_jsonl();
  check(json.find("\"pc\":24") != std::string::npos,
        "trace should include BT/S branch target checkpoint");
}

void test_sh2_bsr_jsr_jmp_and_rts_use_pr_with_delay_slots_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xB003U); // BSR +3 -> 0x000A
  mem.write(0x0002U, 2U, 0x7001U); // ADD #1,R0 (delay)
  mem.write(0x0004U, 2U, 0xE110U); // MOV #16,R1
  mem.write(0x0006U, 2U, 0x410BU); // JSR @R1
  mem.write(0x0008U, 2U, 0x7001U); // ADD #1,R0 (delay)
  mem.write(0x000AU, 2U, 0x7001U); // ADD #1,R0 (BSR target)
  mem.write(0x000CU, 2U, 0x000BU); // RTS
  mem.write(0x000EU, 2U, 0x7001U); // ADD #1,R0 (delay)
  mem.write(0x0010U, 2U, 0x7001U); // ADD #1,R0 (JSR target)
  mem.write(0x0012U, 2U, 0x412BU); // JMP @R1
  mem.write(0x0014U, 2U, 0x7001U); // ADD #1,R0 (delay)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);
  for (int i = 0; i < 18; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(core.reg(0) >= 6U, "BSR/JSR/JMP/RTS flow should execute deterministic delay slots and targets");
  const auto json = trace.to_jsonl();
  check(json.find("\"pc\":16") != std::string::npos,
        "trace should include JMP/JSR target checkpoint deterministically");
  check(json.find("\"pc\":10") != std::string::npos,
        "trace should include BSR target checkpoint with delay-slot semantics");
}

void test_sh2_trapa_vector_fetch_and_rte_scaffold_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U,2U,0xC301U); // TRAPA #1
  mem.write(0x0002U,2U,0x7001U); // ADD #1,R0 (return path)
  mem.write(0x0004U,4U,0x00000010U); // vector 1 -> 0x0010
  mem.write(0x0010U,2U,0x7001U); // ADD #1,R0 (handler)
  mem.write(0x0012U,2U,0x002BU); // RTE

  saturnis::cpu::SH2Core core(0);
  core.reset(0U,0x0001FFF0U);
  for (int i=0;i<7;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));

  check(core.reg(0)>=1U, "TRAPA vector plus RTE scaffold should deterministically execute at least one post-vector instruction");

  const auto json = trace.to_jsonl();
  check(json.find("\"kind\":\"READ\",\"phys\":4") != std::string::npos,
        "TRAPA scaffold should perform deterministic vector-table READ commit");
}

void test_sh2_expanded_mov_addressing_modes_are_deterministic_and_bus_blocking() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U,2U,0xE110U); // MOV #16,R1
  mem.write(0x0002U,2U,0xE280U); // MOV #-128,R2
  mem.write(0x0004U,2U,0x2120U); // MOV.B R2,@R1
  mem.write(0x0006U,2U,0x6310U); // MOV.B @R1,R3
  mem.write(0x0008U,2U,0xE421U); // MOV #0x21,R4
  mem.write(0x000AU,2U,0x2144U); // MOV.B R4,@-R1
  mem.write(0x000CU,2U,0x6514U); // MOV.B @R1+,R5
  mem.write(0x000EU,2U,0xE002U); // MOV #2,R0
  mem.write(0x0010U,2U,0x8012U); // MOV.B R0,@(2,R1)
  mem.write(0x0012U,2U,0x8412U); // MOV.B @(2,R1),R0
  mem.write(0x0014U,2U,0x0009U); // NOP
  mem.write(0x0016U,2U,0x0009U); // NOP
  mem.write(0x0018U,2U,0x9201U); // MOV.W @(1,PC),R2
  mem.write(0x001AU,2U,0xD301U); // MOV.L @(1,PC),R3
  mem.write(0x001CU,2U,0x0009U); // NOP (padding)
  mem.write(0x001EU,2U,0x007FU); // PC-relative word literal
  mem.write(0x0020U,4U,0x12345678U); // PC-relative long literal

  saturnis::cpu::SH2Core core(0);
  core.reset(0U,0x0001FFF0U);
  for (int i=0;i<15;++i) core.step(arbiter, trace, static_cast<std::uint64_t>(i));

  check(mem.read(0x000FU,1U)==0x21U, "MOV.B @-Rn should predecrement and store deterministic byte");
  check(core.reg(1)==0x0010U, "MOV.B @Rm+ should post-increment source register deterministically");
  check(core.reg(3)==0x12345678U, "MOV.L @(disp,PC),Rn should load deterministic literal through blocking bus op");
  check(core.reg(2)==0x0000007FU, "MOV.W @(disp,PC),Rn should sign-extend deterministic literal");

  const auto json = trace.to_jsonl();
  check(json.find("\"kind\":\"READ\"") != std::string::npos,
        "expanded MOV addressing should emit deterministic blocking READ commits");
  check(json.find("\"kind\":\"WRITE\"") != std::string::npos,
        "expanded MOV addressing should emit deterministic blocking WRITE commits");
}

void test_sh2_shift_rotate_subset_updates_t_flag_and_values_deterministically() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE180U); // MOV #-128,R1 -> 0xFFFFFF80
  mem.write(0x0002U, 2U, 0x4100U); // SHLL R1 (T=1)
  mem.write(0x0004U, 2U, 0x4200U); // SHLL R2 (R2 starts 0, T=0)
  mem.write(0x0006U, 2U, 0xE305U); // MOV #5,R3
  mem.write(0x0008U, 2U, 0x4301U); // SHLR R3 (T=1, R3=2)
  mem.write(0x000AU, 2U, 0xE440U); // MOV #64,R4
  mem.write(0x000CU, 2U, 0x4404U); // ROTL R4 (T=0, R4=128)
  mem.write(0x000EU, 2U, 0x4505U); // ROTR R5 (R5 starts 0, T=0)

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);
  for (int i = 0; i < 8; ++i) {
    core.step(arbiter, trace, static_cast<std::uint64_t>(i));
  }

  check(core.reg(1) == 0xFFFFFF00U, "SHLL should shift left and preserve deterministic wrap semantics");
  check(core.reg(2) == 0U, "SHLL on zero should remain zero deterministically");
  check(core.reg(3) == 2U, "SHLR should shift right logically by one");
  check(core.reg(4) == 128U, "ROTL should rotate msb into lsb deterministically");
  check(core.reg(5) == 0U, "ROTR of zero should remain zero deterministically");
  check(core.sr() == 0x000000F0U, "final T-bit should be clear after deterministic rotate-right on zero");

  const auto json = trace.to_jsonl();
  check(json.find("\"sr\":241") != std::string::npos,
        "trace should capture T=1 during shift-edge transitions");
  check(json.find("\"sr\":240") != std::string::npos,
        "trace should capture T=0 during deterministic shift/rotate sequence");
}

void test_sh2_tbit_sett_clrt_movt_and_sr_trace_are_deterministic() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0x0018U); // SETT
  mem.write(0x0002U, 2U, 0x0129U); // MOVT R1
  mem.write(0x0004U, 2U, 0x0008U); // CLRT
  mem.write(0x0006U, 2U, 0x0229U); // MOVT R2

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);
  core.step(arbiter, trace, 3);

  check(core.pc() == 0x0008U, "SETT/CLRT/MOVT should retire and advance PC deterministically");
  check(core.reg(1) == 1U, "MOVT should materialize T=1 after SETT");
  check(core.reg(2) == 0U, "MOVT should materialize T=0 after CLRT");
  check(core.sr() == 0x000000F0U, "T-bit transitions should preserve current modeled SR high bits");

  const auto json = trace.to_jsonl();
  check(json.find("\"sr\":241") != std::string::npos,
        "trace should capture SR T-bit set state deterministically");
  check(json.find("\"sr\":240") != std::string::npos,
        "trace should capture SR T-bit clear state deterministically");
}

void test_sh2_add_immediate_updates_register_with_signed_imm() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE305U); // MOV #5,R3
  mem.write(0x0002U, 2U, 0x73FFU); // ADD #-1,R3
  mem.write(0x0004U, 2U, 0x7302U); // ADD #2,R3

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);

  check(core.pc() == 0x0006U, "ADD #imm should retire and advance PC");
  check(core.reg(3) == 6U, "ADD #imm should use signed 8-bit immediate arithmetic");
}


void test_sh2_add_register_updates_destination() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE105U); // MOV #5,R1
  mem.write(0x0002U, 2U, 0xE3FDU); // MOV #-3,R3
  mem.write(0x0004U, 2U, 0x331CU); // ADD R1,R3

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);

  check(core.pc() == 0x0006U, "ADD Rm,Rn should retire and advance PC");
  check(core.reg(3) == 2U, "ADD Rm,Rn should add source register into destination register");
}


void test_sh2_mov_register_copies_source_to_destination() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  mem.write(0x0000U, 2U, 0xE17BU); // MOV #0x7B,R1
  mem.write(0x0002U, 2U, 0xE200U); // MOV #0,R2
  mem.write(0x0004U, 2U, 0x6213U); // MOV R1,R2

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  core.step(arbiter, trace, 0);
  core.step(arbiter, trace, 1);
  core.step(arbiter, trace, 2);

  check(core.pc() == 0x0006U, "MOV Rm,Rn should retire and advance PC");
  check(core.reg(2) == 0x7BU, "MOV Rm,Rn should copy source register value into destination register");
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
  test_commit_horizon_requires_both_progress_watermarks();
  test_commit_pending_retains_uncommitted_ops();
  test_commit_pending_waits_for_both_progress_watermarks();
  test_commit_pending_preserves_order_of_remaining_ops();
  test_commit_horizon_interleaves_mmio_and_ram_deterministically();
  test_commit_horizon_cycles_progress_with_mixed_pending_ram_and_mmio();
  test_commit_horizon_long_queue_drains_in_three_cycles();
  test_commit_horizon_long_queue_preserves_remaining_order_each_cycle();
  test_commit_horizon_four_cycle_mixed_queue_drain_is_deterministic();
  test_commit_horizon_four_cycle_preserves_queue_order_each_step();
  test_commit_horizon_five_cycle_interleaved_mixed_ops_drain_is_deterministic();
  test_commit_horizon_mmio_read_value_respects_horizon_blocked_write_order();
  test_commit_horizon_asymmetric_progress_updates_before_convergence_are_deterministic();
  test_commit_horizon_six_cycle_alternating_mmio_pressure_drains_deterministically();
  test_commit_horizon_long_sequence_pins_each_queued_mmio_read_value();
  test_commit_horizon_alternating_asymmetric_updates_on_both_cpus_are_deterministic();
  test_commit_horizon_seven_cycle_alternating_ram_mmio_pressure_drain_is_deterministic();
  test_commit_horizon_four_queued_mmio_reads_have_pinned_values();
  test_commit_horizon_progress_alternation_reverses_midway_before_convergence();
  test_scu_overlap_opposite_lane_halfword_set_byte_clear_with_ims_mask_is_deterministic();
  test_scu_overlap_three_batch_alternating_set_clear_has_stable_final_source();
  test_scu_overlap_replayed_ist_clear_preserves_ist_source_agreement();
  test_scu_overlap_write_log_per_cpu_value_histograms_are_stable();
  test_commit_horizon_eight_cycle_mixed_ram_mmio_drain_is_deterministic();
  test_commit_horizon_five_queued_mmio_reads_have_pinned_values();
  test_commit_horizon_progress_reverses_twice_before_convergence();
  test_store_to_load_forwarding();
  test_barrier_does_not_change_contention_address_history();
  test_mmio_write_is_visible_to_subsequent_reads();
  test_mmio_subword_write_updates_correct_lane();
  test_display_status_register_is_read_only_and_ready();
  test_scu_dma_register_file_masks_and_lane_semantics_are_deterministic();
  test_scu_ims_register_masks_to_low_16_bits();
  test_scu_interrupt_pending_respects_mask_and_clear();
  test_scu_interrupt_source_pending_wires_into_ist();
  test_scu_synthetic_source_mixed_size_contention_is_deterministic();
  test_scu_synthetic_source_mixed_size_concurrent_clears_are_deterministic();
  test_scu_synthetic_source_mixed_size_overlapping_clears_are_deterministic();
  test_scu_synthetic_source_overlapping_set_and_clear_same_batch_is_deterministic();
  test_scu_synthetic_source_overlapping_set_and_clear_is_stable_across_runs();
  test_scu_overlapping_set_clear_respects_ims_masked_ist_view();
  test_scu_overlap_set_clear_two_batches_rotate_round_robin_winner();
  test_scu_overlap_set_clear_two_batches_are_stable_across_runs();
  test_scu_overlap_two_batch_different_byte_lanes_is_deterministic();
  test_scu_overlap_staggered_req_time_matches_across_horizon_gating_runs();
  test_scu_overlap_ist_clear_while_masked_then_unmasked_is_deterministic();
  test_scu_overlap_write_log_order_and_payload_are_deterministic();
  test_scu_overlap_byte_halfword_same_batch_has_lane_accurate_ist_visibility();
  test_scu_overlap_replayed_clears_are_idempotent_across_runs();
  test_scu_overlap_alternating_set_clear_bursts_keep_ist_source_consistent();
  test_scu_overlap_write_log_monotonic_deltas_are_stable_across_runs();
  test_scu_overlap_halfword_clear_byte_set_with_ims_mask_is_lane_accurate();
  test_scu_overlap_ist_clear_is_idempotent_with_interleaved_source_set();
  test_scu_overlap_alternating_mask_windows_keep_source_and_ist_consistent();
  test_scu_overlap_write_log_per_cpu_entry_counts_are_stable();
  test_scu_synthetic_source_mixed_cpu_contention_is_deterministic();
  test_scu_synthetic_source_mmio_stall_is_stable_across_runs();
  test_scu_interrupt_source_subword_writes_apply_lane_masks();
  test_scu_interrupt_source_write_log_is_deterministic();
  test_scu_synthetic_source_mmio_commit_trace_order_is_deterministic();
  test_smpc_status_register_is_read_only_and_ready();
  test_vdp2_tvmd_register_masks_to_low_16_bits();
  test_vdp2_tvstat_register_is_read_only_with_deterministic_status();
  test_scsp_mcier_register_masks_to_low_11_bits();
  test_sh2_movl_memory_read_executes_via_bus();
  test_sh2_movw_memory_read_executes_via_bus_with_sign_extend();
  test_sh2_movw_memory_write_executes_via_bus_low_halfword_only();
  test_sh2_movl_memory_write_executes_via_bus();
  test_sh2_bra_uses_delay_slot_deterministically();
  test_sh2_rts_uses_delay_slot_deterministically();
  test_sh2_branch_in_delay_slot_uses_first_branch_target_policy();
  test_sh2_bra_with_movw_delay_slot_applies_branch_after_memory_slot();
  test_sh2_rts_with_movw_delay_slot_applies_branch_after_memory_slot();
  test_sh2_bra_with_movl_delay_slot_applies_branch_after_memory_slot();
  test_sh2_rts_with_movl_delay_slot_applies_branch_after_memory_slot();
  test_sh2_bra_with_movw_store_delay_slot_applies_branch_after_memory_slot();
  test_sh2_rts_with_movw_store_delay_slot_applies_branch_after_memory_slot();
  test_sh2_bra_with_movl_store_delay_slot_applies_branch_after_memory_slot();
  test_sh2_rts_with_movl_store_delay_slot_applies_branch_after_memory_slot();
  test_sh2_bra_delay_slot_store_then_target_store_same_addr_resolves_deterministically();
  test_sh2_rts_delay_slot_store_then_target_store_same_addr_resolves_deterministically();
  test_sh2_bra_delay_slot_movw_then_target_movl_same_addr_overwrite_is_deterministic();
  test_sh2_bra_delay_slot_movl_then_target_movw_same_addr_overwrite_is_deterministic();
  test_sh2_rts_delay_slot_movw_then_target_movl_same_addr_overwrite_is_deterministic();
  test_sh2_rts_delay_slot_movl_then_target_movw_same_addr_overwrite_is_deterministic();
  test_sh2_bra_mixed_width_overwrite_with_negative_immediate_is_deterministic();
  test_sh2_rts_mixed_width_overwrite_with_negative_immediate_is_deterministic();
  test_sh2_mmio_ram_same_address_overwrite_is_todo_and_current_subset_stays_deterministic();
  test_sh2_bra_mixed_width_both_negative_immediates_is_deterministic();
  test_sh2_rts_mixed_width_both_negative_immediates_is_deterministic();
  test_sh2_same_addr_overwrite_with_intermediate_non_memory_instruction_is_deterministic();
  test_sh2_bra_both_negative_mixed_width_with_followup_target_arithmetic_is_deterministic();
  test_sh2_rts_both_negative_mixed_width_with_followup_target_arithmetic_is_deterministic();
  test_sh2_same_addr_overwrite_with_two_intermediate_non_memory_instructions_is_deterministic();
  test_sh2_bra_both_negative_mixed_width_with_dual_target_arithmetic_is_deterministic();
  test_sh2_rts_both_negative_mixed_width_with_dual_target_arithmetic_is_deterministic();
  test_sh2_same_addr_overwrite_with_three_intermediate_non_memory_instructions_is_deterministic();
  test_scu_overlap_non_adjacent_byte_writes_same_batch_are_lane_accurate();
  test_scu_overlap_source_clear_writes_are_idempotent_across_five_runs();
  test_scu_overlap_ist_mask_retention_with_alternating_halfword_ims_writes();
  test_scu_overlap_write_log_address_histograms_are_stable_across_bursts();
  test_commit_horizon_nine_cycle_mixed_ram_mmio_drain_is_deterministic();
  test_commit_horizon_six_queued_mmio_reads_have_pinned_values();
  test_commit_horizon_progress_reverses_on_both_cpus_before_convergence();
  test_sh2_bra_both_negative_overwrite_with_target_register_copy_before_store_is_deterministic();
  test_sh2_rts_both_negative_overwrite_with_target_register_copy_before_store_is_deterministic();
  test_sh2_same_addr_overwrite_with_four_intermediate_non_memory_instructions_is_deterministic();
  test_sh2_tbit_sett_clrt_movt_and_sr_trace_are_deterministic();
  test_sh2_trapa_vector_fetch_and_rte_scaffold_are_deterministic();
  test_sh2_expanded_mov_addressing_modes_are_deterministic_and_bus_blocking();
  test_sh2_shift_rotate_subset_updates_t_flag_and_values_deterministically();
  test_sh2_cmp_eq_and_tst_update_tbit_deterministically();
  test_sh2_bt_bf_and_s_forms_follow_deterministic_branch_rules();
  test_sh2_bsr_jsr_jmp_and_rts_use_pr_with_delay_slots_deterministically();
  test_sh2_add_immediate_updates_register_with_signed_imm();
  test_sh2_add_register_updates_destination();
  test_sh2_mov_register_copies_source_to_destination();
  test_sh2_ifetch_cache_runahead();
  std::cout << "saturnis kernel tests passed\n";
  return 0;
}
