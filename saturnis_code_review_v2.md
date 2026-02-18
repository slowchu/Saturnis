# Saturnis — In-Depth Code Review & Audit (v2)

**Reviewed:** Saturnis-main (research vertical slice)  
**Date:** February 17, 2026  
**Language:** C++20  
**Scope:** Full source tree — `src/`, `tests/`, `docs/`, `CMakeLists.txt`

---

## Executive Summary

Saturnis has a well-conceived deterministic bus-arbitration model, clean naming conventions,
consistent `[[nodiscard]]` discipline, and an impressively broad test surface for the features
it does cover. There are, however, **five correctness bugs that will bite you in non-trivial
programs**, two hard architectural gaps in the SH-2 subset, and several arbiter/progress
invariant edge cases (including a real same-producer monotonicity pitfall). This review
prioritises findings by their impact on the project's stated goals: deterministic dual-CPU
bus ordering and correct SH-2 bring-up tracing.

---

## Severity Legend

| Severity | Meaning |
|----------|---------|
| 🔴 Critical | Correctness bug, data loss, or hang in the core execution path |
| 🟠 High | Incorrect behaviour in reachable paths; fix before expanding features |
| 🟡 Medium | Design flaw or invariant violation with bounded current impact |
| 🔵 Low | Polish, clarity, or minor inefficiency |

---

## Fix Sequencing (Dependency Graph)

Some fixes depend on others. Work in this order to avoid rework:

```
[C1] StoreBuffer drain
    └── [H3] Cache-before-commit (now correctly sized)

[C3] fill_line release guard
    └── [H2] TraceLog thread-safety (safe to call fill_line from thread)

[C4] Exception return (RTE)
    └── [H4] ILLEGAL_OP fault PC (meaningful once RTE is present)

[C2] Fault PC sentinel
    └── [H4] ILLEGAL_OP fault PC (same infrastructure)

[H1] owner_name ≡ provenance_tag dedup
    └── [H5] BusProducer::Auto resolution centralisation

[A3] Monotonicity edge case (same-producer reorder)
    └── [A4] commit_horizon deadlock guard
        └── [H6] Multithread deadlock (now has a diagnostic path)
```

Fix `C1–C4` first. Then `H1–H6` in any order within the batch. Arbiter invariant work
(`A1–A4`) can proceed in parallel once `C2` is done.

---

## 🔴 Critical Issues

---

### C1. `StoreBuffer` Grows Without Bound and Is Never Drained

**File:** `src/mem/memory.cpp`, `src/cpu/scripted_cpu.cpp`

`StoreBuffer::push()` appends an entry for every script write. `apply_response()` only
handles the read path; it never removes the matching entry after a write is acknowledged
by the arbiter. Every write accumulates forever.

`forward()` is a reverse linear scan over the whole deque, so its cost grows with every
write in a session. The existing test for "overflow retention beyond 16 entries" verifies
the buffer *retains* — it does not verify eviction, which does not happen.

**How to reproduce:**

```cpp
// Run 10 000 scripted writes through the arbiter, then profile forward().
// Observe allocation growth; forward() time grows linearly with write count.
std::vector<ScriptOp> ops;
for (int i = 0; i < 10000; ++i)
  ops.push_back({ScriptOpKind::Write, 0x1000U, 4, static_cast<uint32_t>(i), 0});
ScriptedCPU cpu(0, ops);
// After run_pair completes, cpu's internal store_buffer_ has 10 000 entries.
```

**Acceptance criteria:**
- After `apply_response` acknowledges a write, the matching `StoreEntry` is removed from
  the buffer (or marked retired).
- `StoreBuffer::size()` after a full scripted run equals the number of *unacknowledged*
  writes, not the total number of writes.
- All existing forwarding regression tests continue to pass.

**Fix sketch:**

```cpp
// In apply_response, after the arbiter response for a Write:
void ScriptedCPU::apply_response(std::size_t script_index, const bus::BusResponse &) {
  const auto &ins = script_[script_index];
  if (ins.kind == ScriptOpKind::Write) {
    const uint32_t phys = mem::to_phys(ins.vaddr);
    // Drain oldest matching entry (FIFO order matches commit order).
    for (auto it = store_buffer_.entries_.begin(); it != store_buffer_.entries_.end(); ++it) {
      if (it->phys == phys && it->size == ins.size) {
        store_buffer_.entries_.erase(it);
        break;
      }
    }
  }
  // ... existing read path ...
}
```

---

### C2. `fault_response()` Records PC as Hard-Coded Zero

**File:** `src/bus/bus_arbiter.cpp`

```cpp
trace_.add_fault(core::FaultEvent{start, op.cpu_id, 0U, detail, reason});
//                                               ^^^ always zero
```

Every arbiter-level fault — including `INVALID_BUS_OP` and `NON_MONOTONIC_REQ_TIME` — is
recorded with `pc = 0`. The PC is not available at the arbiter level, but emitting zero is
worse than emitting a sentinel because it looks like a valid PC and misleads trace analysis.

**How to reproduce:**

```cpp
BusOp bad{0, 0U, 0, BusKind::Read, 0x00001001U, 4, 0}; // unaligned MMIO read
arbiter.commit(bad);
// Trace contains: FAULT {"pc":0,...,"reason":"INVALID_BUS_OP"}
// 0 looks like the BIOS entry point, not "unknown."
```

**Acceptance criteria:**
- Fault events emitted by the arbiter use `pc = 0xFFFF'FFFF'U` as an explicit
  "unavailable" sentinel, documented in the trace schema.
- Or: callers that do have the PC (e.g. `SH2Core::step`) pass it through to
  `commit(op, hint_pc)`.

---

### C3. `TinyCache::fill_line` — Silent Corruption in Release Builds

**File:** `src/mem/memory.cpp`

```cpp
void TinyCache::fill_line(std::uint32_t line_base,
                          const std::vector<uint8_t> &line_data) {
  assert(line_data.size() == line_size_);  // NDEBUG strips this
  // proceeds regardless of size mismatch
```

If a mismatched line arrives in a release build, the write loop
(`line.bytes = line_data`) silently stores a wrong-sized vector.
Subsequent reads from that cache line return garbage instructions.
This is a silent icache corruption path.

**How to reproduce:**

```cpp
// Build with -DNDEBUG. Pass an 8-byte vector to a 16-byte cache.
TinyCache cache(16, 4);
std::vector<uint8_t> bad(8, 0xAA);
cache.fill_line(0, bad);  // assert stripped; line_data.size() = 8 != 16
uint32_t out = 0;
cache.read(0, 4, out);    // reads from an 8-byte line as if it were 16-byte — UB
```

**Acceptance criteria:**
- A size mismatch in `fill_line` either (a) logs a `FaultEvent` and skips the fill, or
  (b) throws a recoverable exception — never proceeds with the bad data.
- Release builds produce the same observable fault behaviour as debug builds.

**Fix:**

```cpp
void TinyCache::fill_line(std::uint32_t line_base,
                          const std::vector<uint8_t> &line_data) {
  if (line_data.size() != line_size_) {
    // Caller bug: skip fill rather than corrupt.
    return;
  }
  // ... rest unchanged
}
```

---

### C4. Exception Entry Is One-Way: `exception_return_*` Set, `RTE` Never Implemented

**File:** `src/cpu/sh2_core.cpp`, `src/cpu/sh2_core.hpp`

When an exception vector read completes, `apply_ifetch_and_step` saves the return address
and SR:

```cpp
exception_return_pc_ = pc_;
exception_return_sr_ = sr_;
pc_ = response.value;   // jump to handler
```

But `execute_instruction` has no case for `RTE` (opcode `0x002B`). Any code that enters an
exception handler executes indefinitely with no return path. The saved state is never
consumed. If the BIOS exception vectors are ever exercised (e.g. an unimplemented opcode
fault that triggers a CPU exception), the emulator silently loops in the handler forever.

This is architecturally misleading: the data model *looks* like a full exception round-trip,
but only the entry half exists.

**How to reproduce:**

```cpp
// Trigger an illegal-opcode fault path. If the SH-2 hardware exception vector
// is in the loaded BIOS image, produce_until_bus will set pending_exception_vector_,
// which leads to an ExceptionVectorRead -> apply_ifetch_and_step saves PC/SR
// -> pc_ = response.value (handler addr). No RTE ever returns.
```

**Acceptance criteria (choose one):**

Option A — Implement minimal RTE:
```cpp
} else if (instr == 0x002BU) {   // RTE
  pending_branch_target_ = exception_return_pc_;
  sr_ = exception_return_sr_;
  pc_ += 2U;
```
Option B — Fault visibly:
- If an `ExceptionVectorRead` completes and the resulting handler PC is executed, emit a
  `FAULT` event with reason `"EXCEPTION_HANDLER_ENTERED_NO_RTE"` after `N` instructions,
  making the half-implemented state observable in traces.

---

## 🟠 High Priority Issues

---

### H1. `owner_name()` and `provenance_tag()` Are Identical Functions

**File:** `src/bus/bus_op.hpp`

Both functions contain exactly the same logic and return the same values for every input.
The `owner` and `tag` fields in every `COMMIT` trace line will always be equal. If the
intent is for them to diverge (e.g. `tag` distinguishing CPU-initiated vs.
SCU-triggered-DMA), the divergence is missing. If they're meant to be the same, one is
dead code that silently drifts.

**Fix:** Delete one function and use the other everywhere, or implement the
semantic difference that justifies the distinction. Document the intended contract.

---

### H2. `TraceLog` Thread-Safety Contract Is Undocumented

**File:** `src/core/trace.hpp`, `src/core/trace.cpp`

`TraceLog::add_state()` and `add_fault()` are called from `SH2Core::execute_instruction()`.
The multithread path already runs `ScriptedCPU` on producer threads. If `SH2Core` ever
moves into a producer thread, there will be an undetected data race on `lines_`. The class
carries no comment indicating its threading model.

**Fix:** Add a prominent comment:

```cpp
// TraceLog is NOT thread-safe. All methods must be called from the single
// arbiter/coordinator thread. If SH2Core is ever run on a producer thread,
// add external locking before calling add_state/add_fault.
class TraceLog { ... };
```

---

### H3. `ScriptedCPU` Writes Cache Before Bus Commit — Core Invariant Violation

**File:** `src/cpu/scripted_cpu.cpp`

*(Previously rated Low/Polish — reclassified Medium→High: this violates the project's
stated core invariant.)*

```cpp
store_buffer_.push(mem::StoreEntry{phys, ins.size, ins.value});
if (!uncached) {
  cache_.write(phys, ins.size, ins.value);  // ← cache updated at produce time
}
// BusOp emitted here — arbiter has NOT yet committed the write
```

The architecture document states: **"Memory visibility is arbiter commit order, not local
execution order."** Updating the cache at produce time breaks this: a subsequent read from
the same address will hit the cache and return the new value *before the arbiter has
committed the write*. This can mask ordering bugs in test scenarios where a read is
scheduled to observe a write in-flight.

The store buffer's `forward()` already provides the correct local view for the producing
CPU. The cache write is double-updating the local view and adding premature global
visibility.

**How to reproduce:**

```cpp
// CPU0: Write 0xDEAD to 0x1000, then Read 0x1000 (uncached alias of same line)
// With the current bug, the Read hits the data-cache line and returns 0xDEAD
// before the arbiter has processed the Write. With correct semantics, the
// uncached alias forces a bus Read that returns the *committed* value.
```

**Acceptance criteria:**
- Cache is updated only inside `apply_response` when a cacheable write is acknowledged
  (or via cache-line fill on a read miss response).
- Forwarding from the store buffer provides local visibility for the producing CPU.
- A test verifies that a cached read of a written address, before `apply_response`,
  returns the *pre-write* committed value (not the speculative new value).

---

### H4. `ILLEGAL_OP` Fault Records the Wrong PC

**File:** `src/cpu/sh2_core.cpp`

```cpp
} else {
  trace.add_fault(core::FaultEvent{t_, cpu_id_, pc_, ..., "ILLEGAL_OP"});
  pc_ += 2U;   // ← pc_ advanced BEFORE the fault is recorded? No — fault is first
}
```

Actually the fault *is* recorded before `pc_ += 2U`, so the faulting PC is correct here.
However, the fault fires on the *next* instruction after any unrecognised encoding because
the delay-slot path calls `execute_instruction` one extra time for the slot. An illegal
opcode in a delay slot records a fault but the PC points to the delay-slot instruction,
not the branch that caused the slot to be executed. The branch PC is lost.

**Fix:** Pass `branch_pc` into `execute_instruction` and record it in the fault when
executing a delay slot.

---

### H5. `commit_batch` Uses O(n) `vector::erase` Inside a Loop → O(n²) Total

**File:** `src/bus/bus_arbiter.cpp`

```cpp
pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(next_idx));
```

Called once per committed op across a loop of n iterations: O(n) × O(n) = O(n²). The
`commit_pending` accumulation path means `pending` can grow large over a long session.

**Fix:** Mark-and-compact (one pass at end) or swap-and-pop (O(1) per removal):

```cpp
std::swap(pending[next_idx], pending.back());
pending.pop_back();
```

Verify that `pick_next`'s deterministic ordering is preserved after the swap; the
`committable` index list is rebuilt each iteration so it handles this correctly.

---

### H6. Multithread Coordinator — Potential Hang Under Horizon Starvation

**File:** `src/core/emulator.cpp` — `run_scripted_pair_multithread`

The coordinator loop exits only when `done0 && done1 && !p0 && !p1`. If the commit
horizon never advances far enough to make a pending op committable, `commit_batch` returns
empty, `p0`/`p1` never reset, and the loop never terminates. This is a real risk when one
producer thread stalls (e.g. blocked waiting for a response that requires the other CPU's
progress to advance first, creating a circular wait).

Additionally, `wait_spins` and `loop_spins` are monotonically increasing and never reset.
Within 16–32 iterations they reach their yield-trigger modulus on *every* pass, making the
busy-wait degenerate into a pure `yield()` loop for the rest of the run.

**How to reproduce:**

```cpp
// Two CPUs each waiting for the other's progress watermark to advance,
// neither making forward progress: triggers the starvation loop.
// The multithread test with commit_horizon coverage can be extended to
// engineer this by setting one CPU's progress to never advance.
```

**Acceptance criteria:**
- If `commit_batch` returns empty for more than `N` consecutive iterations while both
  `p0` and `p1` are non-null, emit a `FAULT` event with reason
  `"COORDINATOR_STALL_TIMEOUT"` and break.
- Spin counters reset after each successful commit so yield throttling remains proportional
  to actual idle time.

---

## Arbiter & Progress Invariant Section

This section covers edge cases in the determinism and commit-safety model that are not
obviously bugs today but represent fragile invariants as the codebase grows.

---

### A1. Same-Producer Reorder + Commit-Time Monotonic Check: A Real Pitfall

**File:** `src/bus/bus_arbiter.cpp`

The arbiter maintains per-producer monotonic req_time tracking via `producer_last_req_time_`
and `producer_seen_`. A `NON_MONOTONIC_REQ_TIME` fault fires if an op arrives with
`req_time < producer_last_req_time_[slot]`.

The enqueue-time check (`validate_enqueue_contract`) and the commit-time check
(`execute_commit`) are *separate* — and the commit-time check fires **after** the op has
already been batched and selected by `pick_next`. This means:

1. Op A (seq=0, req_time=5) arrives in batch B1.
2. Op B (seq=1, req_time=3) arrives in the *same* batch B1 from the same producer.
3. `pick_next` selects B first (lower start time).
4. Commit-time check fires a `NON_MONOTONIC_REQ_TIME` fault for A (req_time=5 > 3 is fine)
   — wait, actually this scenario faults on B, not A.

The real pitfall: if the same producer submits two ops in the same `commit_batch` call with
non-monotonic req_times, the second op to be *committed* (whichever has the higher
req_time) will fail the monotonic check even though it was submitted first. The fault fires
on a legitimately queued op because the commit order differs from the submission order.

This is precisely the class of false-fault risk documented in the code review history:
"some RTS mixed-width overwrite checks currently encode modeled behaviour."

**Mitigation:** Document explicitly that `commit_batch` callers must pre-sort ops from the
same producer by `req_time` before submission, or that non-monotonic same-producer
req_times within a single batch are a usage contract violation. Add a static assert or
pre-pass sort in `commit_batch`:

```cpp
// Sort same-producer ops by req_time before commit loop to prevent
// false NON_MONOTONIC faults from pick_next reordering.
std::stable_sort(pending.begin(), pending.end(),
  [](const CommitResult &a, const CommitResult &b) {
    if (producer_slot(a.op) == producer_slot(b.op))
      return a.op.req_time < b.op.req_time;
    return false;
  });
```

---

### A2. Progress Watermark Initialisation Race in Multithread Path

**File:** `src/core/emulator.cpp`

In `run_scripted_pair_multithread`, both producer threads start immediately and can push
`PendingBusOp` to the coordinator's queues before the coordinator has called
`update_progress`. The first `commit_batch` call may therefore see `progress_tracking_enabled_
= true` (set by the first `update_progress` call) but only one watermark established,
meaning `has_safe_horizon()` returns false, and `committable` is empty for the first
several loop iterations.

This is not a bug (the loop retries), but it means the first few productive cycles burn
time spinning before any progress is made. Worse, if `done0` fires before `progress0` is
drained, the watermark for CPU0 may never be updated, and `has_safe_horizon()` stays false
indefinitely.

**Fix:** Initialize both watermarks before starting the producer threads:

```cpp
arbiter.update_progress(0, 0U);  // ← already present, correct
arbiter.update_progress(1, 0U);  // ← already present, correct
// Good. But ensure that if a CPU finishes before emitting any progress,
// the coordinator explicitly calls update_progress(cpu_id, cpu.local_time())
// before checking done.
```

---

### A3. `commit_horizon` Returns `min(max, max)` = `max` Before Both Watermarks Exist

**File:** `src/bus/bus_arbiter.cpp`

```cpp
std::array<core::Tick, 2> progress_up_to_{{
  std::numeric_limits<core::Tick>::max(),
  std::numeric_limits<core::Tick>::max()
}};
```

Before `progress_tracking_enabled_` is set (i.e. before any `update_progress` call),
`commit_horizon()` returns `max`. `has_safe_horizon()` returns `true` when tracking is
*disabled*, which means all ops are immediately committable. This is the intended
"no-tracking" fast path.

But once tracking is enabled with only one watermark updated, `commit_horizon()` returns
`min(updated_value, max)` = `updated_value`, which may be very small (e.g. `0` or `1`).
This can cause all pending ops from the non-updated CPU to be blocked, even ops with very
low `req_time` that are safe to commit.

The current `has_safe_horizon()` guard prevents committable selection until *both*
watermarks are non-`max`, which is correct. The edge case is: if CPU1 never calls
`update_progress` (e.g. it has no ops at all), `has_safe_horizon()` returns false
forever, and the batch drains nothing. The test suite covers the case where both CPUs
make progress, but not the case where one CPU is idle from the start.

**Mitigation:** When a CPU reports itself as `done`, immediately update its watermark to
`numeric_limits::max()` — or some sufficiently large "infinity" — so it no longer blocks
the horizon.

---

### A4. `validate_enqueue_contract` Is Reset Per `commit_batch` Call, Not Per Session

**File:** `src/bus/bus_arbiter.cpp`

```cpp
std::vector<CommitResult> BusArbiter::commit_batch(const std::vector<BusOp> &ops) {
  producer_enqueued_seen_.fill(false);
  producer_last_enqueued_req_time_.fill(0U);
  // ...
}
```

The per-call enqueue tracking is reset to zero at the start of every `commit_batch`. This
means a producer can submit `req_time=100` in batch B1, and then `req_time=5` in batch B2,
and the enqueue contract will not fire — because B2's check resets to zero. The
commit-time monotonic check (`producer_last_req_time_`, not reset per batch) *will* catch
this, but it fires as a `NON_MONOTONIC_REQ_TIME` fault rather than an
`ENQUEUE_NON_MONOTONIC_REQ_TIME` fault, losing information about whether the violation
was at enqueue or commit time.

This is a correctness gap in the diagnostic layer, not in the commit outcome. But it
means the "enqueue" vs. "commit" fault distinction is unreliable across batch boundaries.

---

## 🟡 SH-2 Semantics Gaps

The SH-2 interpreter is intentionally minimal, but several gaps affect correctness in ways
that are not obvious from the "vertical slice" framing. These matter most for BIOS
bring-up.

---

### S1. Branch-in-Delay-Slot: "First-Branch-Wins" Is Non-Standard

**File:** `src/cpu/sh2_core.cpp`

The current policy, documented in the architecture notes, is that a branch opcode decoded
in a delay slot is silently ignored — the outer branch target wins. Real SH-2 hardware
raises a *slot illegal instruction* exception for branches in delay slots. The current
behaviour is defined and tested, but it means any BIOS code that relies on the exception
(even to detect programming errors) will silently take the wrong branch instead of
faulting.

**Recommendation:** Keep the current policy for the vertical slice but add a trace
`FAULT` with reason `"BRANCH_IN_DELAY_SLOT"` (distinct from `ILLEGAL_OP`) so trace
analysis can detect when this path is taken. This makes silent divergence visible.

---

### S2. JSR / BSR Not Implemented — PR Loaded Manually in Tests

**File:** `src/cpu/sh2_core.cpp`

`JSR @Rm` (opcode `0x400BU`) is implemented. `BSR disp` (opcode `0xB000`-family, 12-bit
signed displacement) is not. Tests that exercise `RTS` flows set `pr_` directly via
`set_pr()` rather than executing a call instruction, which means the call instruction's
delay-slot semantics and PR-update timing are untested.

If BIOS code uses `BSR`, it will hit the `ILLEGAL_OP` fault path and continue, silently
miscomputing the return address.

---

### S3. No `SUB`, `AND`, `OR`, `XOR` — ALU Coverage Is Asymmetric

**File:** `src/cpu/sh2_core.cpp`

The implemented ALU operations are: `ADD #imm`, `ADD Rm,Rn`, `MOV #imm`, `MOV Rm,Rn`,
shift variants, `CMP/EQ`. There are no subtraction, logical, or multiply instructions.
BIOS initialisation routines universally use `AND` for masking and `OR` for flag setting.
Without these, the SH-2 path can only execute linear "load constants and store" sequences.

This is a known limitation but it substantially restricts which BIOS bring-up paths are
exercisable. The next highest-value instructions to add, in order of BIOS frequency:
`SUB Rm,Rn` (0x3008), `AND Rm,Rn` (0x2009), `OR Rm,Rn` (0x200B), `XOR Rm,Rn` (0x200A).

---

### S4. Unknown Opcode Policy: Fault-and-Continue vs. Fault-and-Halt

**File:** `src/cpu/sh2_core.cpp`

```cpp
} else {
  trace.add_fault(..., "ILLEGAL_OP");
  pc_ += 2U;   // continues past the illegal opcode
}
```

The CPU continues after an unimplemented instruction. This is useful for reaching more
code during bring-up, but it means the CPU state after the fault is architecturally
undefined. Register values may be stale, SR may be wrong, and subsequent correct
instructions execute with corrupt inputs. Faults accumulate silently in the trace and
are easy to miss.

**Recommendation:** Add a `set_halt_on_fault(true)` mode to the SH2Core (mirroring
`TraceLog::halt_on_fault`) and expose it via the BIOS run config. Use it in tests that
are expected to run clean code.

---

## 🟡 Medium Issues

---

### M1. `gbr_` Declared but Never Referenced

**File:** `src/cpu/sh2_core.hpp`

```cpp
std::uint32_t gbr_ = 0;
```

GBR-relative instructions (`MOV.L @(disp,GBR),R0` — `0xC4xx`) are not implemented.
The field silently occupies state. Add a `// TODO: GBR-relative addressing` comment or
remove until needed.

---

### M2. `time.cpp` Is an Empty Compilation Unit

**File:** `src/core/time.cpp`

Contains only `#include "core/time.hpp"`. Remove from the CMakeLists source list and
delete the file; `Tick` is a type alias, the header alone is sufficient.

---

### M3. `maybe_write_trace()` Is Dead Code

**File:** `src/core/emulator.cpp`

The private `Emulator::maybe_write_trace()` method is defined but never called.
`Emulator::run()` manually opens an `ofstream` and writes the trace inline, duplicating
the method's intent. Either call the method or delete it.

---

### M4. `is_mmio()` Hardcoded Range List Will Not Scale

**File:** `src/mem/memory.cpp`

```cpp
bool is_mmio(std::uint32_t phys) {
  return (phys >= 0x05C00000U && phys <= 0x05CFFFFFU) ||
         (phys >= 0x05D00000U && phys <= 0x05DFFFFFU) ||
         (phys >= 0x05F00000U && phys <= 0x05FFFFFFU);
}
```

Three ranges today; every new peripheral adds another branch. Consider a small static
table:

```cpp
struct AddrRange { uint32_t lo, hi; };
static constexpr AddrRange kMmioRanges[] = {
  {0x05C00000U, 0x05CFFFFFU},
  {0x05D00000U, 0x05DFFFFFU},
  {0x05F00000U, 0x05FFFFFFU},
};
```

---

## 🔵 Low / Polish Issues

| # | File | Issue |
|---|------|-------|
| L1 | `bus/bus_arbiter.cpp` | O(n²) `vector::erase` in `commit_batch` — covered in H5 |
| L2 | `mem/memory.cpp` | `read_block` byte-by-byte copy; use `std::copy` |
| L3 | `dev/devices.cpp` | Magic `0xA5000000U` SMPC result sentinel; needs named constant + comment |
| L4 | `src/main.cpp` | `--max-steps` uses `std::stoull` with no user-friendly validation |
| L5 | `bus/bus_op.hpp` | `BusProducer::Auto` resolution duplicated in `producer_slot()`, `owner_name()`, `provenance_tag()` — centralise |
| L6 | `tests/test_kernel.cpp` | `check()` calls `exit(1)` — first failure kills all tests; consider a continue-on-failure runner |
| L7 | `tests/` vs `src/core/emulator.cpp` | `run_pair()` in tests duplicates `run_scripted_pair()` in production; drift in progress-update ordering creates subtle scheduling divergence |

---

## Test Coverage Assessment

**Strengths:**
- Deterministic commit-horizon drain tests are thorough and cover multi-cycle
  progress-watermark scenarios.
- SCU register lane and mask semantics are well-exercised.
- Single-thread vs. multithread parity checks across all commit kinds are a strong
  regression barrier.

**Gaps:**
- No test exercises the `StoreBuffer` under acknowledged-write drain (because drain
  doesn't exist yet — see C1).
- No test for `fill_line` with a mismatched size vector in a release-equivalent build.
- No test for the "idle CPU never calls `update_progress`" scenario (see A3).
- No test for same-producer non-monotonic req_time *within a single batch* (see A1).
- No test verifies that `ScriptedCPU` cache reads after an unacknowledged write return
  the pre-write committed value (the H3 invariant).
- SH-2 test coverage is entirely `ScriptedCPU`-mediated; there are no tests that drive
  `SH2Core` through a realistic multi-instruction sequence with real opcode encodings
  from a memory image (i.e. no "mini-ROM" test).

---

## Summary Table

| ID | File(s) | Issue | Severity |
|----|---------|-------|----------|
| C1 | `mem/memory.cpp`, `cpu/scripted_cpu.cpp` | StoreBuffer never drains | 🔴 |
| C2 | `bus/bus_arbiter.cpp` | Fault PC always 0 | 🔴 |
| C3 | `mem/memory.cpp` | `fill_line` assert-only in release | 🔴 |
| C4 | `cpu/sh2_core.cpp` | Exception return state set, RTE missing | 🔴 |
| H1 | `bus/bus_op.hpp` | `owner_name` ≡ `provenance_tag` | 🟠 |
| H2 | `core/trace.hpp` | Thread-safety contract undocumented | 🟠 |
| H3 | `cpu/scripted_cpu.cpp` | Cache updated before bus commit (invariant violation) | 🟠 |
| H4 | `cpu/sh2_core.cpp` | Delay-slot illegal-op records wrong PC | 🟠 |
| H5 | `bus/bus_arbiter.cpp` | O(n²) erase in `commit_batch` | 🟠 |
| H6 | `core/emulator.cpp` | Multithread coordinator hang under horizon starvation | 🟠 |
| A1 | `bus/bus_arbiter.cpp` | Same-producer reorder triggers false fault | 🟡 |
| A2 | `core/emulator.cpp` | Progress watermark init race in MT path | 🟡 |
| A3 | `bus/bus_arbiter.cpp` | Idle CPU blocks horizon forever | 🟡 |
| A4 | `bus/bus_arbiter.cpp` | Enqueue contract reset per-batch loses cross-batch info | 🟡 |
| S1 | `cpu/sh2_core.cpp` | Branch-in-delay-slot silently diverges from hardware | 🟡 |
| S2 | `cpu/sh2_core.cpp` | BSR unimplemented; RTS tested via manual PR set only | 🟡 |
| S3 | `cpu/sh2_core.cpp` | No SUB/AND/OR/XOR — blocks realistic BIOS paths | 🟡 |
| S4 | `cpu/sh2_core.cpp` | Fault-and-continue masks cascade corruption | 🟡 |
| M1 | `cpu/sh2_core.hpp` | `gbr_` unused | 🟡 |
| M2 | `core/time.cpp` | Empty compilation unit | 🟡 |
| M3 | `core/emulator.cpp` | `maybe_write_trace` dead code | 🟡 |
| M4 | `mem/memory.cpp` | `is_mmio` hardcoded range list | 🟡 |
| L1–L7 | various | Polish / minor issues | 🔵 |

---

*Review produced from static analysis of all source files. No runtime profiling or
BIOS-boot execution was performed in this pass.*
