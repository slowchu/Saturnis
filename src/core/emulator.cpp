#include "core/emulator.hpp"

#include "bus/bus_arbiter.hpp"
#include "cpu/scripted_cpu.hpp"
#include "cpu/sh2_core.hpp"
#include "dev/devices.hpp"
#include "mem/memory.hpp"
#include "platform/file_io.hpp"
#include "platform/sdl_window.hpp"

#include <fstream>
#include <iostream>
#include <vector>

namespace saturnis::core {

namespace {
void run_scripted_pair(cpu::ScriptedCPU &cpu0, cpu::ScriptedCPU &cpu1, bus::BusArbiter &arbiter) {
  while (!(cpu0.done() && cpu1.done())) {
    auto p0 = cpu0.produce();
    auto p1 = cpu1.produce();
    if (!p0 && !p1) {
      break;
    }

    std::vector<std::pair<int, cpu::PendingBusOp>> pending;
    if (p0) {
      pending.emplace_back(0, *p0);
    }
    if (p1) {
      pending.emplace_back(1, *p1);
    }
    std::vector<bus::BusOp> ops;
    ops.reserve(pending.size());
    for (const auto &entry : pending) {
      ops.push_back(entry.second.op);
    }

    const auto committed = arbiter.commit_batch(ops);
    for (const auto &result : committed) {
      const auto &entry = pending[result.input_index];
      const auto &resp = result.response;
      if (entry.first == 0) {
        cpu0.apply_response(entry.second.script_index, resp);
      } else {
        cpu1.apply_response(entry.second.script_index, resp);
      }
    }
  }
}
} // namespace

std::string Emulator::run_dual_demo_trace() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  std::vector<cpu::ScriptOp> cpu0_ops{{cpu::ScriptOpKind::Write, 0x00001000U, 4, 0xDEADBEEFU, 0},
                                      {cpu::ScriptOpKind::Compute, 0, 0, 0, 3},
                                      {cpu::ScriptOpKind::Write, 0x20001000U, 4, 0xC0FFEE11U, 0},
                                      {cpu::ScriptOpKind::Write, 0x05F00020U, 4, 0x1234U, 0}};
  std::vector<cpu::ScriptOp> cpu1_ops{{cpu::ScriptOpKind::Read, 0x00001000U, 4, 0, 0},
                                      {cpu::ScriptOpKind::Compute, 0, 0, 0, 2},
                                      {cpu::ScriptOpKind::Read, 0x20001000U, 4, 0, 0},
                                      {cpu::ScriptOpKind::Read, 0x05F00010U, 4, 0, 0}};

  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair(cpu0, cpu1, arbiter);

  return trace.to_jsonl();
}

void Emulator::maybe_write_trace(const RunConfig &config, const TraceLog &trace) const {
  if (config.trace_path.empty()) {
    return;
  }
  std::ofstream ofs(config.trace_path);
  trace.write_jsonl(ofs);
}

int Emulator::run(const RunConfig &config) {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  if (!config.bios_path.empty()) {
    const auto bios = platform::read_binary_file(config.bios_path);
    for (std::size_t i = 0; i < bios.size(); ++i) {
      mem.write(static_cast<std::uint32_t>(i), 1U, bios[i]);
    }
  }

  if (config.dual_demo || config.bios_path.empty()) {
    std::cout << "Running deterministic dual-CPU demo\n";
    const auto demo_trace = run_dual_demo_trace();
    std::cout << demo_trace;
    if (!config.trace_path.empty()) {
      std::ofstream ofs(config.trace_path);
      ofs << demo_trace;
    }
    return 0;
  }

  cpu::SH2Core master(0);
  cpu::SH2Core slave(1);
  master.reset(0x00000000U, 0x0001FFF0U);
  slave.reset(0x00000000U, 0x0001FFF0U);

  std::uint64_t seq = 0;
  while ((master.executed_instructions() + slave.executed_instructions()) < config.max_steps) {
    const auto p0 = master.produce_until_bus(seq++, trace, 16);
    const auto p1 = slave.produce_until_bus(seq++, trace, 16);

    std::vector<bus::BusOp> fetches;
    if (p0.op.has_value()) {
      fetches.push_back(*p0.op);
    }
    if (p1.op.has_value()) {
      fetches.push_back(*p1.op);
    }

    if (fetches.empty()) {
      if (p0.executed == 0U && p1.executed == 0U) {
        break;
      }
      continue;
    }

    const auto committed = arbiter.commit_batch(fetches);
    for (const auto &result : committed) {
      if (result.op.cpu_id == 0) {
        master.apply_ifetch_and_step(result.response, trace);
      } else {
        slave.apply_ifetch_and_step(result.response, trace);
      }
    }
  }

  std::vector<std::uint32_t> framebuffer(320U * 240U, 0xFF101020U);
  for (const auto &w : dev.writes()) {
    const std::size_t pos = static_cast<std::size_t>((w.addr ^ static_cast<std::uint32_t>(w.t)) % framebuffer.size());
    framebuffer[pos] = 0xFF00FF00U;
  }

  platform::present_framebuffer_if_available(320, 240, framebuffer, config.headless);
  maybe_write_trace(config, trace);
  return 0;
}

} // namespace saturnis::core
