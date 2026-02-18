# Saturnis — Code Review & Audit Report

**Reviewed by:** Claude (Anthropic)  
**Date:** February 17, 2026  
**Codebase:** `github.com/slowchu/Saturnis` — Bus-Authority driven Saturn emulator research prototype  
**Total LOC reviewed:** ~6,056 across 18 source files + 2 test files

---

## Executive Summary

Saturnis is a thoughtfully structured research prototype. The core bus-authority model is architecturally sound and the determinism guarantees it chases are real and well-tested. The test suite is impressively deep — over 150 test cases covering arbitration, horizon gating, MMIO lane semantics, SH-2 delay slots, and multi-run stability.

That said, there are **two correctness bugs** that affect functional accuracy, one of which would silently corrupt execution state. There are also meaningful design-level concerns around the memory endianness model, the store buffer overflow policy, and the multithread coordination loop. These are detailed below, from highest to lowest severity.

---

## Bug Reports

### 🔴 BUG-1: Loads to R15 incorrectly write `pr_` (SH2Core)

**File:** `src/cpu/sh2_core.cpp` — `apply_ifetch_and_step`

```cpp
} else if (pending.kind == PendingMemOp::Kind::ReadLong) {
    r_[pending.dst_reg] = response.value;
    if (pending.dst_reg == 15U) {   // <-- wrong register check
        pr_ = r_[pending.dst_reg];
    }
}
```

This same pattern appears for `ReadWord` and `ReadByte` as well. R15 is the **stack pointer (SP)**; it has nothing to do with PR (the procedure register / return address). Any instruction that loads to R15 — such as `MOV.L @Rm,R15` — will silently overwrite PR with the loaded value. This corrupts the return address of any function call in progress.

The same wrong assumption appears in all three load width handlers. The likely intended logic — if there ever was one — was to handle the `MOV.L @Rm+,R15` (post-increment SP restore after `RTE`) pattern, but even then, writing `pr_` is incorrect.

**Impact:** Any SH-2 code that restores the stack pointer from memory (e.g., after a prologue/epilogue) will have its PR trashed.

**Fix:** Remove all three `if (pending.dst_reg == 15U) { pr_ = ...; }` blocks entirely. PR is set only by `JSR` / `BSR` (which set `pr_ = pc_ + 4`) and by direct `LDS Rm,PR` instructions (not yet implemented).

---

### 🔴 BUG-2: Memory byte order is consistently little-endian; Saturn SH-2 is big-endian

**Files:** `src/mem/memory.cpp` — `CommittedMemory::read`, `CommittedMemory::write`, `TinyCache::read`, `TinyCache::write`

All memory read/write operations assemble multi-byte values little-endian:

```cpp
// CommittedMemory::write
bytes_[phys + i] = (value >> (8U * i)) & 0xFF;   // i=0 → LSB at lowest addr

// CommittedMemory::read
out |= bytes_[phys + i] << (8U * i);              // same convention
```

The Saturn SH-2 is a big-endian architecture. A 16-bit value `0xE140` at address 0 should be stored as `{0xE1, 0x40}` (high byte first). The current implementation stores it as `{0x40, 0xE1}`.

**Why tests pass anyway:** Every test that writes an instruction or data value uses `mem.write(addr, size, value)` — the same API that reads it back. Because both write and read use the same (incorrect) byte order, the round-trip is consistent. The tests are testing internal self-consistency, not Saturn hardware semantics.

**Why this matters for `--bios` mode:** A real Saturn BIOS binary is stored big-endian on disk. Loading it byte-for-byte into `CommittedMemory` and executing with the current implementation will decode completely wrong instructions. The `--bios` path is silently broken for any real BIOS file.

**Fix:** Reverse the byte iteration in all four read/write implementations to use big-endian ordering. Also update all test fixtures that hardcode expected memory values (most will need their byte patterns reversed).

---

## Design-Level Issues

### 🟠 ISSUE-1: `StoreBuffer` silently drops entries at capacity

**File:** `src/mem/memory.cpp` — `StoreBuffer::push`

```cpp
void StoreBuffer::push(StoreEntry entry) {
    entries_.push_back(entry);
    if (entries_.size() > kMaxEntries) {
        entries_.pop_front();   // oldest entry evicted silently
    }
}
```

`kMaxEntries = 16`. When a CPU issues more than 16 uncommitted stores before any of them are committed, the oldest store is silently dropped from the forwarding buffer. If a subsequent load hits that dropped entry's address, it will miss in the store buffer and fall through to committed memory — which does not yet contain the store — producing a stale (wrong) value.

For the current scripted demo this is harmless because no script issues 16+ pending writes. But any realistic workload (e.g., a memcpy) would trigger this. The buffer should either grow dynamically, assert on overflow, or expose the overflow as a detectable event so the caller can stall or flush.

---

### 🟠 ISSUE-2: `commit_batch` has O(n²) inner loop due to `vector::erase`

**File:** `src/bus/bus_arbiter.cpp` — `BusArbiter::commit_batch`

```cpp
pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(next_idx));
```

This is called inside a `while (!pending.empty())` loop. Each `erase` on a `std::vector` is O(n) due to element shifting, making the whole function O(n²). For small batches this is fine, but it will become a bottleneck if batch sizes grow. A better approach: build a separate `committed` vector by index, then use a single `remove_if` pass, or swap-and-pop if ordering doesn't need to be preserved.

---

### 🟠 ISSUE-3: Multithread loop uses unconstrained busy-wait

**File:** `src/core/emulator.cpp` — `run_scripted_pair_multithread`

Both producer threads and the arbiter main loop spin on `std::this_thread::yield()` with no backoff. Under CPU contention (e.g., running tests on a loaded CI machine), the spinning will degrade responsiveness and can cause test flakiness due to scheduling jitter. Given that determinism is the project's core goal, any non-deterministic timing in the test loop is a risk. Consider a condition variable or semaphore to block the main loop until either producer pushes a message.

---

### 🟡 ISSUE-4: `is_uncached_alias` uses a simplified/incorrect SH-2 memory map

**File:** `src/mem/memory.cpp`

```cpp
bool is_uncached_alias(std::uint32_t vaddr) {
    return (vaddr & 0x20000000U) != 0U;
}
```

On the actual SH-2 (and SH7604 as used in the Saturn):
- `P0/U0`: `0x00000000–0x1FFFFFFF` — cacheable  
- `P1`: `0x80000000–0x9FFFFFFF` — cacheable, physical  
- `P2`: `0xA0000000–0xBFFFFFFF` — **uncached**, physical  
- `P3`: `0xC0000000–0xDFFFFFFF` — cacheable  
- `P4`: `0xE0000000–0xFFFFFFFF` — on-chip I/O, not cacheable  

The current mask `0x20000000` treats addresses like `0x20000000–0x3FFFFFFF` as uncached, but these are still in the P0 area and should be cacheable. The real uncached alias is `0xA0000000`. This is a simplification documented in the README but it means the scripted demo's `0x20001000` test address is classified as uncached when it should be cached.

---

### 🟡 ISSUE-5: `SCU IST` (0x05FE00A4) is writable for set — non-standard behavior

**File:** `src/dev/devices.cpp` — `DeviceHub::write`

```cpp
if (word_addr == kScuIstAddr) {
    const std::uint32_t masked_bits = write_bits & 0x0000FFFFU;
    scu_interrupt_pending_ |= masked_bits;   // write SETS interrupt pending
    return;
}
```

On real Saturn hardware, IST is a read-only status register. You cannot set interrupt pending bits by writing to 0x05FE00A4 — they are set by the hardware as interrupts occur, and cleared by writing to the IST-clear register (0x05FE00A8). The current model allows software to set pending bits by writing to IST, which is non-standard. This is likely intentional for test convenience (several tests do `commit({..., MmioWrite, kScuIstAddr, ...})`), but it should be flagged as a deliberate deviation in a comment.

---

### 🟡 ISSUE-6: `to_phys` discards the entire P1–P4 region distinction

**File:** `src/mem/memory.cpp`

```cpp
std::uint32_t to_phys(std::uint32_t vaddr) {
    return vaddr & 0x1FFFFFFFU;
}
```

This maps all virtual addresses to a 512 MB physical space by stripping the top 3 bits. P1 (cacheable, `0x8xxxxxxx`) and P2 (uncached, `0xAxxxxxxx`) both map to the same physical address via this mask, which is correct. However, P4 (`0xE0000000–0xFFFFFFFF`) — the SH-2's on-chip control registers (cache control, interrupt control, FRT, etc.) — also gets stripped to `0x00000000–0x1FFFFFFF`, which is wrong. Any attempt to access on-chip registers would silently alias into system RAM. For this prototype it likely doesn't matter, but it's a trap if someone adds SH-2 on-chip peripheral emulation.

---

### 🟡 ISSUE-7: `BusOp::size` is unchecked — silent out-of-bounds on `CommittedMemory`

**File:** `src/mem/memory.cpp` — `CommittedMemory::read` and `write`

```cpp
for (std::size_t i = 0; i < size; ++i) {
    out |= bytes_[phys + i] << (8U * i);
}
```

`size` is a `uint8_t` with no runtime validation that it equals 1, 2, or 4. A `BusOp` with `size = 0` would read nothing, returning 0. A `BusOp` with `size = 3` or `size = 5` would read a non-power-of-2 width, producing results that have no meaning on the target hardware. The `access_in_range` helper prevents out-of-bounds accesses, but there's no assertion that size is a legal SH-2 access width. A `switch` or `assert` would catch bad ops early.

---

### 🟡 ISSUE-8: `produce_until_bus` `runahead_budget` does not account for pending ops re-entry

**File:** `src/cpu/sh2_core.cpp` — `produce_until_bus`

When `pending_mem_op_` is set, the function returns immediately without consuming any budget. When `pending_exception_vector_` is set, same. Only the inner for-loop respects the budget. This means calling `produce_until_bus` with `budget=1` will always return the pending memory op immediately and never run ahead — which is the correct and intended behavior for `SH2Core::step`. But `run_bios_trace` calls it with `budget=16`, which means after a cache line fill, up to 15 instructions may execute before the next bus sync. If those 15 instructions include a branch that causes the fetched cache line to be irrelevant, the executed instructions still advance `pc_` and `t_`, but the `seq` counter in the `BusOp` is fixed at the value passed into `produce_until_bus`. Subsequent ops from that call path will share a stale `seq`. This is probably harmless since `seq` is only used for tie-breaking determinism, but it's worth flagging.

---

## Code Quality Observations

### Positive Observations

- **Namespace hygiene** is excellent throughout (`saturnis::bus`, `saturnis::cpu`, etc.)
- **`[[nodiscard]]` is used consistently** on all value-returning methods that have important results. This is professional-grade.
- **Unsigned arithmetic is handled carefully** — near-zero cases and max-tick sentinel values use `numeric_limits<Tick>::max()` correctly.
- **`BusArbiter::pick_next`** is a clean, deterministic tournament implementation. The priority class comparison, tie-break round-robin, and sequence-number tiebreaker form a well-defined total order.
- **The `TraceLog` JSONL format** is a good choice for this domain — easy to diff, easy to parse, and appending is O(1).
- **`DeviceHub`'s `lane_shift` + `size_mask` model** for sub-word MMIO accesses is the right approach and is well-exercised by tests.
- **The test suite is unusually thorough** for a research prototype. The multi-run stability pattern (`baseline` on run 0, compare on runs 1–4) is a solid way to catch non-determinism.
- **`commit_pending`'s index remapping** through `was_committed` + rebuild is correct and clear.

### Minor Code Style Issues

**Inconsistent formatting in test file** — late tests in `test_kernel.cpp` abandon the one-statement-per-line style (e.g., `for (int i=0;i<7;++i) core.step(...)`). The early tests are much more readable. Worth normalizing.

**`emulator.cpp` `maybe_write_trace` is unused** — it's defined but never called. `run` handles trace writing inline. This is dead code.

**`source_name` in `bus_op.hpp` duplicates logic from `kind_name`** for non-DMA ops. If the `BusKind` enum grows, this function would need to be updated in tandem. Consider deriving `source_name` from `kind_name` for the non-DMA cases.

**`SH2Core::set_pr` is public but only used in tests** to set up PR before an RTS. Since this bypasses the normal JSR flow, it's a test-only escape hatch. Consider marking it or wrapping it.

---

## Architecture Review

### The Bus-Authority Model

The single-arbiter design is correct for its stated goal: producing a **deterministic total order** over all memory operations regardless of which thread/CPU requested them. The progress watermark / commit horizon mechanism (`update_progress` → `commit_horizon` → horizon gating in `commit_batch`) is the project's core innovation and it's well-implemented.

One subtlety worth documenting: the horizon condition is `req_time < horizon` (strict less-than). An op with `req_time == horizon` is **not** committed. This means that if CPU-0 is at tick 10 and CPU-1 is at tick 10, the horizon is 10, and any op from either CPU timestamped at exactly tick 10 is held. This is the right conservative choice — you can't commit an op at tick 10 until both CPUs have proven they have no earlier op — but the off-by-one is non-obvious and should have a comment.

### The Scripted CPU vs. Real CPU Duality

`ScriptedCPU` and `SH2Core` share a common protocol (produce a `BusOp`, receive a `BusResponse`) but have separate store buffers and caches. Importantly, `ScriptedCPU` has a cache but `SH2Core` only has an instruction cache (`icache_`) — it has no data cache. This asymmetry means the two CPU models have different forwarding behavior for data. For a research prototype this is fine, but it means you cannot mix scripted and real CPUs in a dual-CPU configuration and get consistent semantics.

### Missing from the SH-2 Instruction Decoder

The comment in `execute_instruction` already acknowledges this is a minimal subset. For completeness, the missing instructions most likely to affect real BIOS boot are:

- `BT / BF` (conditional branches) — without these, any BIOS loop is unexitable
- `LDS.L @Rm+,PR` — BIOS prologue/epilogue standard pattern  
- `STS PR,Rn` — reading PR into a register
- `MOV.L @(disp,PC),Rn` — PC-relative load (heavily used for literal pools in BIOS)
- `CMP/GT`, `CMP/GE`, `CMP/HI`, `CMP/HS` — the other comparison forms
- `SUB Rm,Rn`, `NEG`, `NOT`
- `AND / OR / XOR`

Without `BT/BF`, the BIOS demo can execute linear code but cannot follow any branch taken at runtime.

---

## Summary Table

| ID | Severity | Category | File | Description |
|----|----------|----------|------|-------------|
| BUG-1 | 🔴 Critical | Correctness | `sh2_core.cpp` | Load to R15 corrupts PR |
| BUG-2 | 🔴 Critical | Correctness | `memory.cpp` | Little-endian model breaks real BIOS |
| ISSUE-1 | 🟠 High | Correctness | `memory.cpp` | StoreBuffer silently drops overflow entries |
| ISSUE-2 | 🟠 High | Performance | `bus_arbiter.cpp` | O(n²) commit_batch due to vector erase |
| ISSUE-3 | 🟠 Medium | Reliability | `emulator.cpp` | Multithread arbiter loop busy-waits with no backoff |
| ISSUE-4 | 🟡 Medium | Accuracy | `memory.cpp` | Simplified uncached alias detection |
| ISSUE-5 | 🟡 Medium | Accuracy | `devices.cpp` | IST write-to-set is non-standard |
| ISSUE-6 | 🟡 Low | Accuracy | `memory.cpp` | P4 on-chip I/O aliases into RAM |
| ISSUE-7 | 🟡 Low | Robustness | `memory.cpp` | BusOp size not validated |
| ISSUE-8 | 🟡 Low | Subtlety | `sh2_core.cpp` | seq counter stale during runahead |

---

*End of review. The codebase is well above average for a research prototype. The two critical bugs are straightforward to fix. The architectural choices are sound and the test depth is commendable.*
