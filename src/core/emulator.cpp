#include "core/emulator.hpp"

#include "bus/bus_arbiter.hpp"
#include "cpu/scripted_cpu.hpp"
#include "cpu/sh2_core.hpp"
#include "dev/devices.hpp"
#include "mem/memory.hpp"
#include "platform/file_io.hpp"
#include "platform/sdl_window.hpp"

#include <atomic>
#include <deque>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace saturnis::core {

namespace {

struct ScriptResponse {
  std::size_t script_index = 0;
  std::uint64_t producer_token = 0;
  bus::BusResponse response{};
};

class SignalHub {
public:
  void notify() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++epoch_;
    cv_.notify_all();
  }

  void wait_for_change(std::uint64_t &seen_epoch) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this, seen_epoch] { return epoch_ != seen_epoch; });
    seen_epoch = epoch_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::uint64_t epoch_ = 0;
};

template <typename T> class Mailbox {
public:
  void push(const T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(value);
  }

  [[nodiscard]] bool try_pop(T &out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
  }

private:
  std::mutex mutex_;
  std::deque<T> queue_;
};

void run_scripted_pair(cpu::ScriptedCPU &cpu0, cpu::ScriptedCPU &cpu1, bus::BusArbiter &arbiter, core::TraceLog &trace) {
  std::optional<cpu::PendingBusOp> p0;
  std::optional<cpu::PendingBusOp> p1;

  while (true) {
    if (!p0 && !cpu0.done()) {
      p0 = cpu0.produce();
    }
    if (!p1 && !cpu1.done()) {
      p1 = cpu1.produce();
    }

    arbiter.update_progress(0, p0 ? (p0->op.req_time + 1U) : cpu0.local_time());
    arbiter.update_progress(1, p1 ? (p1->op.req_time + 1U) : cpu1.local_time());

    if (!p0 && !p1 && cpu0.done() && cpu1.done()) {
      break;
    }

    std::vector<bus::BusOp> pending_ops;
    std::vector<int> pending_cpu;
    std::vector<std::size_t> pending_script;
    std::vector<std::uint64_t> pending_token;
    if (p0) {
      pending_ops.push_back(p0->op);
      pending_cpu.push_back(0);
      pending_script.push_back(p0->script_index);
      pending_token.push_back(p0->op.producer_token);
    }
    if (p1) {
      pending_ops.push_back(p1->op);
      pending_cpu.push_back(1);
      pending_script.push_back(p1->script_index);
      pending_token.push_back(p1->op.producer_token);
    }

    if (pending_ops.empty()) {
      continue;
    }

    const auto committed = arbiter.commit_batch(pending_ops);
    for (const auto &result : committed) {
      const int cpu = pending_cpu[result.input_index];
      const auto script_index = pending_script[result.input_index];
      const auto token = pending_token[result.input_index];
      if (cpu == 0) {
        cpu0.apply_response(script_index, result.response, token, &trace);
        p0.reset();
      } else {
        cpu1.apply_response(script_index, result.response, token, &trace);
        p1.reset();
      }
    }
  }
}

void run_scripted_pair_multithread(cpu::ScriptedCPU &cpu0, cpu::ScriptedCPU &cpu1, bus::BusArbiter &arbiter, core::TraceLog &trace) {
  arbiter.update_progress(0, 0U);
  arbiter.update_progress(1, 0U);

  Mailbox<cpu::PendingBusOp> req0;
  Mailbox<cpu::PendingBusOp> req1;
  Mailbox<ScriptResponse> resp0;
  Mailbox<ScriptResponse> resp1;
  Mailbox<core::Tick> progress0;
  Mailbox<core::Tick> progress1;

  std::atomic<bool> done0{false};
  std::atomic<bool> done1{false};
  SignalHub signal;

  auto producer = [&trace, &signal](int cpu_id, cpu::ScriptedCPU &cpu, Mailbox<cpu::PendingBusOp> &req, Mailbox<ScriptResponse> &resp,
                     Mailbox<core::Tick> &progress, std::atomic<bool> &done) {
    std::optional<cpu::PendingBusOp> waiting;
    std::uint64_t seen_epoch = 0U;
    while (true) {
      if (!waiting && !cpu.done()) {
        waiting = cpu.produce();
        if (waiting) {
          req.push(*waiting);
          progress.push(waiting->op.req_time + 1);
          signal.notify();
        }
      }

      if (!waiting && cpu.done()) {
        progress.push(cpu.local_time());
        done.store(true);
        signal.notify();
        return;
      }

      ScriptResponse response;
      if (resp.try_pop(response)) {
        cpu.apply_response(response.script_index, response.response, response.producer_token, nullptr);
        progress.push(cpu.local_time());
        waiting.reset();
        signal.notify();
        continue;
      }

      signal.wait_for_change(seen_epoch);
      (void)cpu_id;
    }
  };

  std::thread t0(producer, 0, std::ref(cpu0), std::ref(req0), std::ref(resp0), std::ref(progress0), std::ref(done0));
  std::thread t1(producer, 1, std::ref(cpu1), std::ref(req1), std::ref(resp1), std::ref(progress1), std::ref(done1));

  std::optional<cpu::PendingBusOp> p0;
  std::optional<cpu::PendingBusOp> p1;
  std::uint64_t seen_epoch = 0U;

  while (true) {
    bool progressed = false;
    if (!p0) {
      cpu::PendingBusOp msg;
      if (req0.try_pop(msg)) {
        p0 = msg;
        progressed = true;
      }
    }
    if (!p1) {
      cpu::PendingBusOp msg;
      if (req1.try_pop(msg)) {
        p1 = msg;
        progressed = true;
      }
    }

    core::Tick prog = 0;
    while (progress0.try_pop(prog)) {
      arbiter.update_progress(0, prog);
      progressed = true;
    }
    while (progress1.try_pop(prog)) {
      arbiter.update_progress(1, prog);
      progressed = true;
    }

    std::vector<bus::BusOp> pending_ops;
    std::vector<int> pending_cpu;
    std::vector<std::size_t> pending_script;
    std::vector<std::uint64_t> pending_token;
    if (p0) {
      pending_ops.push_back(p0->op);
      pending_cpu.push_back(0);
      pending_script.push_back(p0->script_index);
      pending_token.push_back(p0->op.producer_token);
    }
    if (p1) {
      pending_ops.push_back(p1->op);
      pending_cpu.push_back(1);
      pending_script.push_back(p1->script_index);
      pending_token.push_back(p1->op.producer_token);
    }

    const bool waiting_for_peer = (p0 && !p1 && !done1.load()) || (p1 && !p0 && !done0.load());
    const bool no_pending = !p0 && !p1;

    if (!pending_ops.empty()) {
      const auto committed = arbiter.commit_batch(pending_ops);
      for (const auto &result : committed) {
        const int cpu = pending_cpu[result.input_index];
        const auto script_index = pending_script[result.input_index];
        if (cpu == 0) {
          resp0.push(ScriptResponse{script_index, pending_token[result.input_index], result.response});
          p0.reset();
        } else {
          resp1.push(ScriptResponse{script_index, pending_token[result.input_index], result.response});
          p1.reset();
        }
        progressed = true;
      }
      if (!committed.empty()) {
        signal.notify();
      }
    }

    if (done0.load() && done1.load() && !p0 && !p1) {
      break;
    }

    if (!progressed && (waiting_for_peer || no_pending)) {
      signal.wait_for_change(seen_epoch);
    }
  }

  t0.join();
  t1.join();
}


std::pair<std::vector<cpu::ScriptOp>, std::vector<cpu::ScriptOp>> contention_stress_scripts() {
  std::vector<cpu::ScriptOp> cpu0_ops;
  std::vector<cpu::ScriptOp> cpu1_ops;
  cpu0_ops.reserve(128U);
  cpu1_ops.reserve(128U);

  for (std::uint32_t i = 0; i < 32U; ++i) {
    const std::uint32_t ram_addr = 0x00002000U + ((i % 4U) * 4U);
    const std::uint32_t mmio_addr = 0x05FE00ACU + ((i % 2U) * 4U);

    cpu0_ops.push_back({cpu::ScriptOpKind::Write, ram_addr, 4U, 0x10000000U + i, 0U});
    cpu0_ops.push_back({cpu::ScriptOpKind::Read, ram_addr, 4U, 0U, 0U});
    cpu0_ops.push_back({cpu::ScriptOpKind::Write, mmio_addr, 4U, 0x00000001U << (i % 8U), 0U});
    cpu0_ops.push_back({cpu::ScriptOpKind::Barrier, 0U, 0U, 0U, 0U});

    cpu1_ops.push_back({cpu::ScriptOpKind::Read, ram_addr, 4U, 0U, 0U});
    cpu1_ops.push_back({cpu::ScriptOpKind::Write, ram_addr, 4U, 0x20000000U + i, 0U});
    cpu1_ops.push_back({cpu::ScriptOpKind::Read, mmio_addr, 4U, 0U, 0U});
    cpu1_ops.push_back({cpu::ScriptOpKind::Barrier, 0U, 0U, 0U, 0U});
  }

  return {cpu0_ops, cpu1_ops};
}

std::pair<std::vector<cpu::ScriptOp>, std::vector<cpu::ScriptOp>> vdp1_source_event_stress_scripts() {
  std::vector<cpu::ScriptOp> cpu0_ops;
  std::vector<cpu::ScriptOp> cpu1_ops;

  for (std::uint32_t cmd = 1U; cmd <= 6U; ++cmd) {
    cpu0_ops.push_back({cpu::ScriptOpKind::Write, 0x05D00098U, 4U, cmd, 0U});
    cpu0_ops.push_back({cpu::ScriptOpKind::Write, 0x05D000A0U, 4U, 1U, 0U});
    cpu0_ops.push_back({cpu::ScriptOpKind::Read, 0x05D00094U, 4U, 0U, 0U});

    cpu1_ops.push_back({cpu::ScriptOpKind::Read, 0x05FE00A4U, 4U, 0U, 0U});
    cpu1_ops.push_back({cpu::ScriptOpKind::Write, 0x05FE00A8U, 4U, 0x00000020U, 0U});
    cpu1_ops.push_back({cpu::ScriptOpKind::Read, 0x05D0009CU, 4U, 0U, 0U});
  }

  return {cpu0_ops, cpu1_ops};
}

std::pair<std::vector<cpu::ScriptOp>, std::vector<cpu::ScriptOp>> vdp1_source_event_stress_scripts_cpu1_owner() {
  std::vector<cpu::ScriptOp> cpu0_ops;
  std::vector<cpu::ScriptOp> cpu1_ops;

  for (std::uint32_t cmd = 1U; cmd <= 6U; ++cmd) {
    cpu1_ops.push_back({cpu::ScriptOpKind::Write, 0x05D00098U, 4U, cmd, 0U});
    cpu1_ops.push_back({cpu::ScriptOpKind::Write, 0x05D000A0U, 4U, 1U, 0U});
    cpu1_ops.push_back({cpu::ScriptOpKind::Read, 0x05D00094U, 4U, 0U, 0U});

    cpu0_ops.push_back({cpu::ScriptOpKind::Read, 0x05FE00A4U, 4U, 0U, 0U});
    cpu0_ops.push_back({cpu::ScriptOpKind::Write, 0x05FE00A8U, 4U, 0x00000020U, 0U});
    cpu0_ops.push_back({cpu::ScriptOpKind::Read, 0x05D0009CU, 4U, 0U, 0U});
  }

  return {cpu0_ops, cpu1_ops};
}

std::pair<std::vector<cpu::ScriptOp>, std::vector<cpu::ScriptOp>> dual_demo_scripts() {
  std::vector<cpu::ScriptOp> cpu0_ops{{cpu::ScriptOpKind::Write, 0x00001000U, 4, 0xDEADBEEFU, 0},
                                      {cpu::ScriptOpKind::Compute, 0, 0, 0, 3},
                                      {cpu::ScriptOpKind::Write, 0x20001000U, 4, 0xC0FFEE11U, 0},
                                      {cpu::ScriptOpKind::Write, 0x05F00020U, 4, 0x1234U, 0}};
  std::vector<cpu::ScriptOp> cpu1_ops{{cpu::ScriptOpKind::Read, 0x00001000U, 4, 0, 0},
                                      {cpu::ScriptOpKind::Compute, 0, 0, 0, 2},
                                      {cpu::ScriptOpKind::Read, 0x20001000U, 4, 0, 0},
                                      {cpu::ScriptOpKind::Read, 0x05F00010U, 4, 0, 0}};
  return {cpu0_ops, cpu1_ops};
}

} // namespace

std::string Emulator::run_dual_demo_trace() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = dual_demo_scripts();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}

std::string Emulator::run_dual_demo_trace_multithread() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = dual_demo_scripts();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair_multithread(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}


std::string Emulator::run_contention_stress_trace() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = contention_stress_scripts();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}

std::string Emulator::run_contention_stress_trace_multithread() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = contention_stress_scripts();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair_multithread(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}

std::string Emulator::run_vdp1_source_event_stress_trace() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = vdp1_source_event_stress_scripts();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}

std::string Emulator::run_vdp1_source_event_stress_trace_multithread() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = vdp1_source_event_stress_scripts();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair_multithread(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}

std::string Emulator::run_vdp1_source_event_stress_trace_cpu1_owner() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = vdp1_source_event_stress_scripts_cpu1_owner();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}

std::string Emulator::run_vdp1_source_event_stress_trace_cpu1_owner_multithread() {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  const auto [cpu0_ops, cpu1_ops] = vdp1_source_event_stress_scripts_cpu1_owner();
  cpu::ScriptedCPU cpu0(0, cpu0_ops);
  cpu::ScriptedCPU cpu1(1, cpu1_ops);
  run_scripted_pair_multithread(cpu0, cpu1, arbiter, trace);

  return trace.to_jsonl();
}

std::string Emulator::run_bios_trace(const std::vector<std::uint8_t> &bios_image, std::uint64_t max_steps) {
  TraceLog trace;
  mem::CommittedMemory mem;
  dev::DeviceHub dev;
  bus::BusArbiter arbiter(mem, dev, trace);

  for (std::size_t i = 0; i < bios_image.size(); ++i) {
    mem.write(static_cast<std::uint32_t>(i), 1U, bios_image[i]);
  }

  cpu::SH2Core master(0);
  cpu::SH2Core slave(1);
  master.reset(0x00000000U, 0x0001FFF0U);
  slave.reset(0x00000000U, 0x0001FFF0U);

  std::uint64_t seq = 0;
  std::optional<bus::BusOp> p0;
  std::optional<bus::BusOp> p1;

  while ((master.executed_instructions() + slave.executed_instructions()) < max_steps) {
    if (!p0) {
      const auto next = master.produce_until_bus(seq++, trace, 16);
      if (next.op.has_value()) {
        p0 = *next.op;
      }
    }
    if (!p1) {
      const auto next = slave.produce_until_bus(seq++, trace, 16);
      if (next.op.has_value()) {
        p1 = *next.op;
      }
    }

    arbiter.update_progress(0, p0 ? (p0->req_time + 1U) : master.local_time());
    arbiter.update_progress(1, p1 ? (p1->req_time + 1U) : slave.local_time());

    std::vector<bus::BusOp> fetches;
    std::vector<int> cpus;
    if (p0) {
      fetches.push_back(*p0);
      cpus.push_back(0);
    }
    if (p1) {
      fetches.push_back(*p1);
      cpus.push_back(1);
    }

    if (fetches.empty()) {
      if ((master.executed_instructions() + slave.executed_instructions()) >= max_steps) {
        break;
      }
      continue;
    }

    const auto committed = arbiter.commit_batch(fetches);
    if (committed.empty()) {
      continue;
    }
    for (const auto &result : committed) {
      if (cpus[result.input_index] == 0) {
        master.apply_ifetch_and_step(result.response, trace);
        p0.reset();
      } else {
        slave.apply_ifetch_and_step(result.response, trace);
        p1.reset();
      }
    }
  }

  // Deterministic DMA probe for BIOS fixture evolution: one MMIO write/read pair
  // routed through the DMA producer path for stable trace coverage.
  (void)arbiter.commit_dma({0, 0U, seq++, bus::BusKind::MmioWrite, 0x05FE00ACU, 4, 0x00000031U});
  (void)arbiter.commit_dma({0, 1U, seq++, bus::BusKind::MmioRead, 0x05FE00ACU, 4, 0U});

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

  const auto bios = platform::read_binary_file(config.bios_path);
  const auto bios_trace = run_bios_trace(bios, config.max_steps);

  if (!config.trace_path.empty()) {
    std::ofstream ofs(config.trace_path);
    ofs << bios_trace;
  }

  std::vector<std::uint32_t> framebuffer(320U * 240U, 0xFF101020U);
  platform::present_framebuffer_if_available(320, 240, framebuffer, config.headless);
  return 0;
}

} // namespace saturnis::core
