> Archived/Superseded: retained for historical context. Active planning lives in `docs/roadmap_ymir_tool.md`.

# Saturnis → Ymir Roadmap Goal Task List (Revised)

This checklist translates the updated roadmap into concrete execution steps for delivering a **Ymir-usable bus arbitration tool** (not a full emulator core).

## Track A (Active / Required for Delivery)

- [ ] **A1: Finish and harden `libbusarb` extraction**
  - [ ] Ensure `busarb` builds as a standalone static library target.
  - [ ] Keep public headers under `include/busarb/` free of Saturnis-internal types.
  - [ ] Keep tests runnable without BIOS/ROM/device-router dependencies.
  - [x] Verify the library can be linked into a minimal external harness (non-Saturnis test app).

- [ ] **A2: Lock callback-based timing interface**
  - [ ] Keep `TimingCallbacks::access_cycles(...)` as the timing source abstraction.
  - [ ] Confirm `addr`, `is_write`, and `size_bytes` are passed through unchanged.
  - [x] Document callback expectations and ownership/lifetime of callback context.
  - [x] Document callback determinism expectations (same input → same timing result).
  - [x] Document behavior if callback returns invalid/zero/unsupported timing values.

- [ ] **A3: Lock public POD seam types**
  - [ ] Finalize `BusMasterId` values (`SH2_A`, `SH2_B`, `DMA`) and extension guidance.
  - [ ] Finalize `BusRequest` fields (`master_id`, `addr`, `is_write`, `size_bytes`, `now_tick`).
  - [ ] Finalize `BusWaitResult` semantics (`should_wait`, stall-only `wait_cycles`).
  - [ ] **Define `now_tick` semantics precisely**:
    - [ ] units (cycles / ticks)
    - [ ] monotonicity requirements
    - [ ] who owns time advancement (caller vs library)
    - [x] whether repeated queries at the same tick are valid
  - [ ] **Define `wait_cycles` semantics precisely**:
    - [x] whether `wait_cycles == 0` always implies `should_wait == false`
    - [x] whether nonzero `wait_cycles` is ever valid when `should_wait == false` (ideally no)
    - [x] whether `wait_cycles` means “minimum cycles until potentially grantable” vs “predicted delay under no new contenders”

- [ ] **A4: Enforce query/commit arbitration semantics**
  - [ ] Keep `query_wait(...)` non-mutating and call-order independent.
  - [ ] Keep `commit_grant(...)` as the only mutating path.
  - [ ] Keep deterministic tie-break policy documented (`DMA > SH2_A > SH2_B`) for current phase.
  - [ ] Ensure arbitration outcome is not dependent on caller query order within the same `now_tick`.
  - [ ] **Document API preconditions / misuse behavior**:
    - [x] whether `commit_grant(...)` requires a prior `query_wait(...)`
    - [x] behavior on duplicate `commit_grant(...)` for the same request
    - [ ] behavior on committing a request with mismatched fields/tick
    - [ ] whether misuse is debug-asserted, error-coded, or undefined
  - [ ] **Document fairness/starvation behavior**
    - [ ] fixed-priority behavior for current implementation
    - [ ] known starvation risks under heavy DMA
    - [ ] note if fairness/round-robin is deferred

- [ ] **A5: Add optional trace hooks (recommended, non-blocking)**
  - [ ] Add optional `on_grant(...)` callback without affecting core behavior.
  - [ ] Ensure no-op/disabled tracing path remains deterministic and lightweight.
  - [ ] Document trace callback ordering and whether callbacks are allowed to mutate external state.
  - [ ] Keep trace hooks outside core arbitration decisions (observability only).

- [ ] **A6: Complete seam validation suite (must-have)**
  - [ ] Determinism: identical sequences produce identical outputs/state.
  - [ ] Query order independence: contender query order does not change results.
  - [ ] Commit determinism: committing the same winner yields same follow-up waits.
  - [ ] Busy-bus timing correctness: `wait_cycles` matches callback timing behavior.
  - [ ] Tie-break correctness: winner follows policy, not caller order.
  - [x] Repeated-query stability: repeated `query_wait(...)` at same tick (no commit) is stable.
  - [ ] API misuse coverage (debug/assert or documented behavior) for invalid commit sequences.
  - [x] Same-tick multi-contender scenarios across `SH2_A`, `SH2_B`, and `DMA`.

- [ ] **A7: Publish handoff-ready Ymir integration notes**
  - [ ] Explain what `libbusarb` does (stall oracle / arbitration seam only).
  - [ ] Show where Ymir should call `query_wait()` and `commit_grant()`.
  - [ ] Document required callback wiring (`access_cycles`, callback context, etc.).
  - [x] Document **`now_tick` contract** and expected caller behavior.
  - [x] Document **`wait_cycles` interpretation** for Ymir integration.
  - [ ] Document known limitations (no MA/IF stage model yet).
  - [ ] **Document a minimal Ymir adapter pattern**:
    - [x] how to batch contender queries per tick
    - [x] how to choose/commit winner deterministically
    - [x] how to avoid call-order artifacts if Ymir queries in fixed order

---

## Track B (Blocked Until Alignment With Striker)

- [ ] **B1: Get explicit requirement from Striker on MA/IF modeling depth**
  - [ ] Capture written answer: stall-outcome-only vs explicit IF/MA mechanism modeling.
  - [ ] Record the decision in project docs/roadmap notes.
  - [ ] Confirm whether MA/IF expectations apply to initial handoff or later phase (Y5).

- [ ] **B2: Define Ymir-side instrumentation contract**
  - [ ] Agree on metadata needed for chosen MA/IF approach:
    - [ ] `ma_cycles`
    - [ ] `if_cycles`
    - [ ] cache-hit/bus-hit indicators (if needed)
    - [ ] any opcode-derived MA classification fields (if needed)
  - [ ] Define tick semantics at the integration boundary (Ymir tick ↔ libbusarb `now_tick`).
  - [ ] Identify where metadata originates in Ymir (core vs bus layer).
  - [ ] Decide whether metadata is carried on requests, callbacks, or side-channel instrumentation.

- [ ] **B3: Implement chosen MA/IF policy after B1/B2**
  - [ ] If **outcome model**: implement timing-rule policy and manual-sequence tests.
  - [ ] If **mechanism model**: implement explicit IF/MA contention and dedicated tests.
  - [ ] Add tests covering `IF` vs `if` (no bus cycle) behavior if modeled.
  - [ ] Document assumptions and known deviations from hardware/manual.

---

## Track C (Explicitly Optional / Not Blocking Ymir)

- [ ] Decode-family completion for Saturnis emulator goals.
- [ ] Opcode enum + dispatch table refactor.
- [ ] SH-2 execution refactor in `sh2_core.cpp`.
- [ ] Full Saturnis emulator CPU front-end cleanup for production-readability.
- [ ] Any work that does not improve `libbusarb` handoff value or seam correctness.

---

## Freeze Rules While Track A Is Active

- [ ] Do not prioritize full emulator build-out work ahead of Track A completion.
- [ ] Do not expand decode/dispatch refactors unless needed for approved Track B experiments.
- [ ] Keep changes focused on Ymir handoff value and deterministic seam behavior.
- [ ] Do not let optional tracing or future MA/IF designs alter Track A public seam semantics without review.
- [ ] Any new API field added to Track A seam types must come with tests + documentation updates.

---

## Definition of “Ready to Hand Off to Striker” (Track A Exit Criteria)

- [ ] `libbusarb` builds standalone and links in a minimal external harness.
- [ ] Public headers are Saturnis-internal-type-free.
- [ ] `query_wait(...)` / `commit_grant(...)` semantics are documented and tested.
- [ ] Tick and `wait_cycles` semantics are explicit and unambiguous.
- [ ] Determinism + query-order-independence tests pass.
- [ ] Handoff notes include a concrete Ymir adapter pattern and known limitations.
- [ ] MA/IF depth is explicitly marked as deferred pending Striker alignment (Track B).
