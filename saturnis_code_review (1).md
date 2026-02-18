# Saturnis — In-Depth Code Review & Audit

**Reviewed:** Saturnis-main (research vertical slice)
**Date:** February 17, 2026
**Language:** C++20
**Scope:** Full source tree — `src/`, `tests/`, `docs/`, `CMakeLists.txt`

---

## Executive Summary

Saturnis is a well-structured, intentionally scoped research prototype. The deterministic bus-arbitration model is architecturally sound, and the naming conventions, `[[nodiscard]]` usage, and compiler warning setup reflect good C++ practice. That said, there are **several correctness bugs, a category of latent memory issues, dead code, and structural testing concerns** that should be addressed before this codebase grows further. The most important findings are ranked below.

---

## Severity Legend

| Severity | Meaning |
|----------|---------|
| 🔴 Critical | Correctness bug or data-loss risk |
| 🟠 High | Incorrect behavior in some paths; should fix soon |
| 🟡 Medium | Design flaw or performance problem with real impact |
| 🔵 Low | Polish, clarity, or minor inefficiency |

---

## 🔴 Critical Issues

### 1. `StoreBuffer` Grows Without Bound and Is Never Drained

**File:** `src/mem/memory.cpp` + `src/cpu/scripted_cpu.cpp`

`StoreBuffer::push()` appends entries indefinitely. `forward()` scans the entire deque on every read. There is no eviction, no capacity cap, and — critically — **no drain when a Write `BusOp` is committed by the arbiter**.

In `ScriptedCPU::produce()`, a write simultaneously pushes to the store buffer AND emits a `BusOp`. But `apply_response()` only handles the read path; it never removes the corresponding entry from the store buffer. Every write accumulates forever.

The current code review docs do mention a test for "overflow retention beyond 16 entries," but that test verifies the buffer *retains* old entries — it doesn't test that old entries are ever evicted. In a long-running scenario, `forward()` becomes an unbounded linear scan over an ever-growing history.

**Fix:** Track a "committed" watermark or, on each `apply_response` for a write, remove the matching entry. Alternatively, add a hard capacity with proper eviction semantics and document those semantics explicitly.

```cpp
// In apply_response, after a write is acknowledged:
// store_buffer_.drain_up_to(script_index); // or equivalent
```

---

### 2. `fault_response()` Logs PC as Hard-Coded Zero

**File:** `src/bus/bus_arbiter.cpp` — `BusArbiter::fault_response()`

```cpp
trace_.add_fault(core::FaultEvent{start, op.cpu_id, 0U, detail, reason});
//                                               ^^ always zero
```

Every fault event emitted by the arbiter records `pc = 0`. Since the arbiter doesn't have access to the CPU's PC, this is architecturally understandable, but it makes fault traces almost useless for debugging. There is no indication in the trace that the PC field is intentionally absent.

**Fix:** Either pass the PC at the call sites that do have it (`commit` callers know it), or use a sentinel like `0xFFFFFFFFU` and document it in the trace schema as "PC unavailable at arbiter level."

---

### 3. `TinyCache::fill_line` — Silent Corruption in Release Builds

**File:** `src/mem/memory.cpp`

```cpp
void TinyCache::fill_line(std::uint32_t line_base, const std::vector<std::uint8_t> &line_data) {
  assert(line_data.size() == line_size_);  // stripped in release
  // proceeds unconditionally
```

If `line_data.size() != line_size_` in a release build, the cache line is partially overwritten with garbage or truncated data. This corrupts the instruction cache silently. This can be triggered via `apply_ifetch_and_step` whenever the arbiter returns a mismatched line.

**Fix:** Replace the `assert` with a runtime guard:

```cpp
if (line_data.size() != line_size_) {
  // log a fault or simply skip the fill
  return;
}
```

---

### 4. `exception_return_pc_` / `exception_return_sr_` Are Set But Never Read

**File:** `src/cpu/sh2_core.cpp` + `src/cpu/sh2_core.hpp`

In `apply_ifetch_and_step`, when an exception vector read completes:

```cpp
if (pending.kind == PendingMemOp::Kind::ExceptionVectorRead) {
  exception_return_pc_ = pc_;
  exception_return_sr_ = sr_;
  pc_ = response.value;
}
```

These fields are written, but there is no instruction handling for `RTE` (return from exception) anywhere in `execute_instruction`. The exception vector mechanism is half-implemented: the CPU jumps to the handler but can never return. This means any code path that exercises an exception vector read will permanently redirect execution with no way back.

**Fix:** Either implement `RTE` (opcode `0x002B`) in `execute_instruction`, or add a `TODO` comment and a fault for the half-implemented state so it's visible in traces when triggered.

---

## 🟠 High Priority Issues

### 5. `owner_name()` and `provenance_tag()` Are Identical Functions

**File:** `src/bus/bus_op.hpp`

Both functions return `"DMA"` or `"CPU"` based on exactly the same condition. They produce identical output for every possible input. This is confirmed by looking at the trace output — the `owner` and `tag` fields in every `COMMIT` line will always be equal.

```cpp
inline std::string_view owner_name(const BusOp &op) { /* returns "DMA" or "CPU" */ }
inline std::string_view provenance_tag(const BusOp &op) { /* exactly the same logic */ }
```

If these were intended to diverge (e.g., `tag` distinguishing CPU vs. DMA vs. SCU-triggered DMA), the divergence is missing. If they're meant to be the same, one is dead code.

**Fix:** Either delete one and use the other everywhere, or implement the semantic difference that justifies having two.

---

### 6. `Emulator::maybe_write_trace()` Is Dead Code

**File:** `src/core/emulator.cpp` + `src/core/emulator.hpp`

The private method `maybe_write_trace()` is defined but never called anywhere. `Emulator::run()` manually opens a `std::ofstream` and writes the trace inline instead. This is inconsistent and the method is wasted surface area.

```cpp
// Defined but never invoked:
void Emulator::maybe_write_trace(const RunConfig &config, const TraceLog &trace) const { ... }
```

**Fix:** Either call `maybe_write_trace()` from `run()` (replacing the inline file writes), or delete the method.

---

### 7. `run_scripted_pair_multithread` — Potential Deadlock Under Horizon Starvation

**File:** `src/core/emulator.cpp`

In the multithread path, the coordinator loop breaks only when `done0 && done1 && !p0 && !p1`. However, if the commit horizon never advances far enough to make a pending op committable (e.g., one producer thread stalls and stops sending progress updates), `committed` comes back empty, `p0`/`p1` never reset, and the loop never terminates.

The single-threaded `run_scripted_pair` has the same structural issue but it's more obvious to detect because it's sequential. In the multithreaded version, the deadlock is non-deterministic and hard to reproduce.

Additionally, `wait_spins` and `loop_spins` are monotonically increasing integers that are never reset. After ~8–16 iterations they yield on every pass, effectively making the busy-wait a `yield`-loop from the start.

**Fix:** Add a max-wait timeout or a stall detection counter with a diagnostic `FAULT` emission, and reset spin counters after each successful commit.

---

### 8. `commit_batch` Uses O(n) `vector::erase` Inside a Loop → O(n²) Total

**File:** `src/bus/bus_arbiter.cpp`

```cpp
pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(next_idx));
```

This is called once per committed op. For a batch of n ops, this is O(n) erasure × O(n) iterations = O(n²) total. For the research prototype with small batches this is benign, but the `commit_pending` path accumulates ops over time and could surface this.

**Fix:** Use index-based "soft delete" (mark as committed, skip in future passes) or swap-and-pop:

```cpp
std::swap(pending[next_idx], pending.back());
pending.pop_back(); // O(1)
```

Note: swap-and-pop changes iteration order, so verify that `pick_next` still produces deterministic selection after the swap.

---

## 🟡 Medium Issues

### 9. `gbr_` Declared in `SH2Core` but Never Referenced

**File:** `src/cpu/sh2_core.hpp`

```cpp
std::uint32_t gbr_ = 0;  // declared
```

The GBR (Global Base Register) is declared as a member but no instruction in `execute_instruction` reads or writes it. Instructions that use GBR (like `MOV.L @(disp,GBR),R0` — opcode family `0xC4xx`) are not implemented. This is likely intentional for the vertical slice but should have a comment.

**Fix:** Add a `// TODO: GBR-relative instructions not yet implemented` comment, or remove the field until it's needed.

---

### 10. `time.cpp` Is an Empty Compilation Unit

**File:** `src/core/time.cpp`

The file contains only:
```cpp
#include "core/time.hpp"
```

It exists purely to satisfy the CMake source list. This adds a pointless compilation unit with no content. Since `Tick` is a type alias, the header alone is sufficient.

**Fix:** Remove `time.cpp` from the CMakeLists source list and delete the file, or add a comment explaining why it exists.

---

### 11. `CommittedMemory::read_block` Copies Byte-by-Byte

**File:** `src/mem/memory.cpp`

```cpp
for (std::size_t i = 0; i < size; ++i) {
  out[i] = bytes_[phys + static_cast<std::uint32_t>(i)];
}
```

This is called on every cache miss to fill a 16-byte cache line. `std::copy` or `std::memcpy` is semantically cleaner and may be meaningfully faster for the hot path.

```cpp
std::copy(bytes_.begin() + phys, bytes_.begin() + phys + size, out.begin());
```

---

### 12. `BusArbiter` Has No Max-Batch-Size Guard

**File:** `src/bus/bus_arbiter.cpp`

`commit_batch` accepts an arbitrary-length `std::vector<BusOp>`. There is no upper bound. If a caller accidentally passes a very large batch (e.g., a bug in progress-watermark accounting that allows a thousand buffered ops), the O(n²) erase loop and the `committable` vector re-scan will hang.

**Fix:** Add a `SATURNIS_MAX_BATCH_SIZE` compile-time constant and assert/clamp at the entry point.

---

### 13. `TraceLog` Has No Thread-Safety Annotation

**File:** `src/core/trace.hpp` / `src/core/trace.cpp`

`TraceLog::add_state()` and `add_fault()` are called from `SH2Core::execute_instruction()`. If `SH2Core` is ever moved into a producer thread (as `ScriptedCPU` already is in the multithread path), there will be a data race on `lines_`. Currently the SH2 path is single-threaded, but the class gives no indication of its thread-safety contract.

**Fix:** Add a `// NOT thread-safe — call only from arbiter/coordinator thread` comment, and consider a `std::mutex` guard if the SH2 multi-thread path is ever explored.

---

## 🔵 Low / Polish Issues

### 14. `is_valid_bus_op` Deliberately Skips Alignment Checks for RAM

**File:** `src/bus/bus_arbiter.cpp`

The comment reads "Keep current SH-2 RAM subset behavior (which includes existing unaligned RAM tests)." This means unaligned 32-bit RAM reads/writes pass validation silently. Real SH-2 hardware would raise an address error. This should be an explicit architectural note with a `TODO` for when stricter validation is desired.

---

### 15. `ScriptedCPU` Store Buffer Write — Cache Updated Before Bus Commit

**File:** `src/cpu/scripted_cpu.cpp`

```cpp
store_buffer_.push(...)
if (!uncached) {
  cache_.write(phys, ins.size, ins.value);  // cache updated immediately
}
// ... then BusOp is emitted
```

The cache is updated at *produce* time, before the arbiter has acknowledged the write. A later read from the cache will return the speculative value even if the bus write is re-ordered or stalled. For the current scripted tests this doesn't matter (reads are serialized after writes), but it breaks the invariant that "cache reflects committed state."

---

### 16. `SMPC` / `VDP1` Result Encoding Is a Magic Number Constant

**File:** `src/dev/devices.cpp`

```cpp
smpc_command_result_ = 0xA5000000U | command_byte;
```

The `0xA5` prefix has no comment explaining its origin or meaning. In a real Saturn SMPC, response formats are documented. Even in a prototype, this should have a named constant and a comment.

```cpp
// SMPC synthetic command result: high byte 0xA5 is a deterministic prototype sentinel.
// Real SMPC result encoding is command-dependent; TODO: per-command decode.
constexpr std::uint32_t kSmpcSyntheticResultTag = 0xA5000000U;
```

---

### 17. `main.cpp` — `--max-steps` Has No Input Validation

**File:** `src/main.cpp`

```cpp
cfg.max_steps = static_cast<std::uint64_t>(std::stoull(argv[++i]));
```

`std::stoull` throws `std::invalid_argument` or `std::out_of_range` on bad input, but these are `std::exception` subclasses so the outer `catch` handles them. However, the user sees a cryptic "Fatal: stoull" message rather than a helpful "invalid value for --max-steps." A specific catch here would improve UX.

---

### 18. Test Framework Lacks Isolation — First Failure Aborts All Tests

**File:** `tests/test_kernel.cpp`

```cpp
void check(bool cond, const std::string &msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    std::exit(1);  // kills the entire process
  }
}
```

Any single failing assertion terminates the entire test executable. This means a failure in test #1 prevents tests #2–N from running. Diagnosing multiple simultaneous regressions requires multiple fix-rebuild-run cycles.

The test files are also very large (~4,000 lines for `test_kernel.cpp`) with all tests in one `main()` at the bottom. This makes it hard to run individual tests in isolation.

**Consider:** Replacing the custom `check()` harness with a lightweight framework that catches exceptions and continues (even a minimal hand-rolled runner that catches and reports, then continues to the next test).

---

### 19. `run_pair()` in Tests Duplicates `run_scripted_pair()` in Emulator

**File:** `tests/test_kernel.cpp` vs. `src/core/emulator.cpp`

The internal `run_pair()` helper in the test file is functionally identical to `run_scripted_pair()` in `emulator.cpp`, with a slightly different progress-update ordering. This drift means tests may exercise a subtly different scheduling path than production code.

**Fix:** Export `run_scripted_pair()` (or a testable version of it) from `saturnis_core` and have tests call it directly.

---

## Architecture Observations

### Dual-SH2 Progress Watermark Model

The commit-horizon mechanism is well-designed. The invariant that "an op with `req_time < horizon` is safe to commit" is correct and the progress-tracking guards in `has_safe_horizon()` prevent premature commits. The main risk is the deadlock case described in issue #7.

### MMIO Address Range Definition

`is_mmio()` in `memory.hpp` checks three hardcoded physical ranges. This works for the prototype but will need restructuring as more peripherals are added. Consider a range table or a `std::span<AddressRange>` lookup to avoid adding more branches.

### `BusProducer::Auto` Resolution

The `producer_slot()` and `owner_name()` functions both resolve `BusProducer::Auto` by inspecting `cpu_id`. This is implicit logic spread across two places. As DMA paths grow, this "auto" disambiguation will become a source of bugs. Recommend either always setting `producer` explicitly before entering the arbiter, or centralizing the resolution in one private method.

---

## Summary Table

| # | File(s) | Issue | Severity |
|---|---------|-------|----------|
| 1 | `mem/memory.cpp`, `cpu/scripted_cpu.cpp` | StoreBuffer never drains | 🔴 Critical |
| 2 | `bus/bus_arbiter.cpp` | `fault_response` PC always 0 | 🔴 Critical |
| 3 | `mem/memory.cpp` | `fill_line` assert-only in release | 🔴 Critical |
| 4 | `cpu/sh2_core.cpp` | Exception return state set, never used | 🔴 Critical |
| 5 | `bus/bus_op.hpp` | `owner_name` ≡ `provenance_tag` | 🟠 High |
| 6 | `core/emulator.cpp` | `maybe_write_trace` dead code | 🟠 High |
| 7 | `core/emulator.cpp` | Multithread deadlock under horizon stall | 🟠 High |
| 8 | `bus/bus_arbiter.cpp` | O(n²) `erase` in `commit_batch` | 🟠 High |
| 9 | `cpu/sh2_core.hpp` | `gbr_` unused | 🟡 Medium |
| 10 | `core/time.cpp` | Empty compilation unit | 🟡 Medium |
| 11 | `mem/memory.cpp` | `read_block` byte-by-byte copy | 🟡 Medium |
| 12 | `bus/bus_arbiter.cpp` | No batch-size guard | 🟡 Medium |
| 13 | `core/trace.hpp` | Thread-safety contract undocumented | 🟡 Medium |
| 14 | `bus/bus_arbiter.cpp` | Unaligned RAM silently allowed | 🔵 Low |
| 15 | `cpu/scripted_cpu.cpp` | Cache written before bus commit | 🔵 Low |
| 16 | `dev/devices.cpp` | Magic number in SMPC result | 🔵 Low |
| 17 | `src/main.cpp` | `--max-steps` no input validation | 🔵 Low |
| 18 | `tests/test_kernel.cpp` | `exit(1)` kills all tests on first failure | 🔵 Low |
| 19 | `tests/` vs `core/emulator.cpp` | `run_pair` duplicated from production | 🔵 Low |

---

## Recommended Fix Order

1. **StoreBuffer drain** — fix first; it's a silent memory accumulation bug in all paths.
2. **`fill_line` release-build corruption** — one-line fix with significant safety value.
3. **Exception return state** — add `RTE` or a fault trap; leaving it half-done is misleading.
4. **`fault_response` PC=0** — pass PC at call sites or document the sentinel.
5. **`owner_name`/`provenance_tag` dedup** — low-effort, clarifies the data model.
6. **`maybe_write_trace` dead code** — trivial to fix, reduces confusion.
7. **Multithread deadlock** — add a stall detection path before expanding the MT model further.
8. **O(n²) erase** — fix before adding DMA batch paths.

---

*Review produced from static analysis of all source files. No runtime profiling or BIOS-boot execution was performed.*
