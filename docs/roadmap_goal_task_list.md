# Saturnis → Ymir Roadmap Goal Task List

This checklist translates the new roadmap into concrete execution steps.

## Track A (Active / Required for Delivery)

- [ ] **A1: Finish and harden `libbusarb` extraction**
  - [ ] Ensure `busarb` builds as a standalone static library target.
  - [ ] Keep public headers under `include/busarb/` free of Saturnis-internal types.
  - [ ] Keep tests runnable without BIOS/ROM/device-router dependencies.

- [ ] **A2: Lock callback-based timing interface**
  - [ ] Keep `TimingCallbacks::access_cycles(...)` as the timing source abstraction.
  - [ ] Confirm `addr`, `is_write`, and `size_bytes` are passed through unchanged.
  - [ ] Document callback expectations and ownership/lifetime of callback context.

- [ ] **A3: Lock public POD seam types**
  - [ ] Finalize `BusMasterId` values (`SH2_A`, `SH2_B`, `DMA`) and extension guidance.
  - [ ] Finalize `BusRequest` fields (`master_id`, `addr`, `is_write`, `size_bytes`, `now_tick`).
  - [ ] Finalize `BusWaitResult` semantics (`should_wait`, stall-only `wait_cycles`).

- [ ] **A4: Enforce query/commit arbitration semantics**
  - [ ] Keep `query_wait(...)` non-mutating and call-order independent.
  - [ ] Keep `commit_grant(...)` as the only mutating path.
  - [ ] Keep deterministic tie-break policy documented (`DMA > SH2_A > SH2_B`).

- [ ] **A6: Complete seam validation suite (must-have)**
  - [ ] Determinism: identical sequences produce identical outputs/state.
  - [ ] Query order independence: contender query order does not change results.
  - [ ] Commit determinism: committing the same winner yields same follow-up waits.
  - [ ] Busy-bus timing correctness: `wait_cycles` matches callback timing.
  - [ ] Tie-break correctness: winner follows policy, not caller order.

- [ ] **A7: Publish handoff-ready Ymir integration notes**
  - [ ] Explain what `libbusarb` does (stall oracle only).
  - [ ] Show where Ymir should call `query_wait()` and `commit_grant()`.
  - [ ] Document required callback wiring.
  - [ ] Document known limitations (no MA/IF stage model yet).

- [ ] **A5: Add optional trace hooks (recommended)**
  - [ ] Add optional `on_grant(...)` callback without affecting core behavior.
  - [ ] Ensure no-op/disabled tracing path remains deterministic and lightweight.

## Track B (Blocked Until Alignment With Striker)

- [ ] **B1: Get explicit requirement from Striker on MA/IF modeling depth**
  - [ ] Capture written answer: stall-outcome-only vs explicit IF/MA mechanism modeling.
  - [ ] Record the decision in project docs/roadmap notes.

- [ ] **B2: Define Ymir-side instrumentation contract**
  - [ ] Agree on metadata (`ma_cycles`, `if_cycles`, cache-hit/bus-hit indicators, tick semantics).
  - [ ] Identify where metadata originates in Ymir (core vs bus layer).

- [ ] **B3: Implement chosen MA/IF policy after B1/B2**
  - [ ] If outcome model: implement timing-rule policy and manual-sequence tests.
  - [ ] If mechanism model: implement explicit IF/MA contention and dedicated tests.

## Track C (Explicitly Optional / Not Blocking Ymir)

- [ ] Decode-family completion for Saturnis emulator goals.
- [ ] Opcode enum + dispatch table refactor.
- [ ] SH-2 execution refactor in `sh2_core.cpp`.

## Freeze Rules While Track A Is Active

- [ ] Do not prioritize full emulator build-out work ahead of Track A completion.
- [ ] Do not expand decode/dispatch refactors unless needed for approved Track B experiments.
- [ ] Keep changes focused on Ymir handoff value and deterministic seam behavior.
