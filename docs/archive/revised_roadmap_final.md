> Archived/Superseded: retained for historical context. Active planning lives in `docs/roadmap_ymir_tool.md`.

# Saturnis Revised Roadmap — libbusarb as Reference Tool

**Revision date:** 2026-02-22
**Supersedes:** `docs/Roadmap.md`, `docs/roadmap_goal_task_list.md`, `docs/saturnis_roadmap_unified.md`

---

## North Star

> A standalone, deterministic **bus-timing reference tool** that produces **comparable, calibratable stall estimates** for traced Sega Saturn bus accesses (for the contention scenarios currently modeled), which StrikerX3 can use to validate and calibrate Ymir's built-in timing without runtime performance cost in release builds.

### What changed

StrikerX3 clarified two things that pivot the entire project:

1. **"The stall timing is more important"** — He wants correct numbers, not a mechanistic simulation of IF/MA stages competing. The arbiter is a timing oracle, not a pipeline model.

2. **"The SH-2 interpreter is consistently at the top of every profiling session I ran"** — Any per-instruction overhead in Ymir's hot path is unacceptable. The arbiter cannot run inline in the emulation loop at production speed.

This means **Phase 1 treats libbusarb as a development-time reference/replay tool (offline-first)**. Runtime integration into Ymir is out of scope for the initial handoff and would require separate overhead validation. The workflow is: record bus access traces from Ymir → replay through the arbiter → compare stall counts → use deltas to improve Ymir's built-in approximate timing tables.

### What this is NOT

- A required runtime component in Ymir's release builds (Phase 1 is offline-first)
- A full Saturn emulator
- A pipeline simulator
- An SH-2 interpreter (Ymir already has one with 3,582 commits behind it)

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│                      YMIR (runtime)                          │
│                                                              │
│  SH2.Advance() ──→ IsBusWait() ──→ approximate stall table   │
│       │                                                      │
│       ├──→ [optional] emit trace record per bus access       │
│       │   (addr, size, r/w, master, service_cycles, retries, │
│       │    tick_first_attempt, tick_complete)                │
│       │                                                      │
│  SCU.RunDMA() ──→ IsBusWait() ──→ approximate stall table    │
│       │                                                      │
│       └──→ [optional] emit trace record per DMA word         │
│                                                              │
│  Output: bus_trace.jsonl                                     │
└──────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────────┐
│                  SATURNIS / libbusarb (offline)              │
│                                                              │
│  1. Load bus_trace.jsonl                                     │
│  2. Replay each record through Arbiter                       │
│     - query_wait() → should_wait / wait_cycles               │
│     - commit_grant() → advance bus_free_tick                 │
│  3. Compare arbiter timing vs Ymir trace-derived timing      │
│  4. Output: diff report (per-access delta, histogram, etc.)  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────────┐
│               StrikerX3 reviews diff report                  │
│                                                              │
│  - Identifies regions where Ymir's approximate timing is off │
│  - Tunes Ymir's built-in tables / IsBusWait logic            │
│  - Re-records trace, re-replays, iterates                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

---

## Current State Assessment

### What exists and is sound

| Component | Status | Notes |
|-----------|--------|-------|
| `busarb` CMake target | ✅ Done | Builds as standalone static lib |
| Public header `include/busarb/busarb.hpp` | ✅ Done | No Saturnis-internal types |
| `TimingCallbacks::access_cycles` | ✅ Done | C function pointer, void* ctx |
| `BusRequest` / `BusWaitResult` POD types | ✅ Done | Clean, minimal |
| `query_wait()` (non-mutating) | ✅ Done | Const, order-independent |
| `commit_grant()` (mutating) | ✅ Done | Only state-changing path |
| `pick_winner()` (deterministic tie-break) | ✅ Done | DMA > SH2_A > SH2_B |
| `bus_free_tick()` accessor | ✅ Done | |
| Link harness test | ✅ Done | Proves external linkability |
| Busarb unit tests | ✅ Done | 5 tests covering core contracts |
| Internal `BusArbiter` (Saturnis-side) | ✅ Done | Full batch/pending/horizon model |
| JSONL trace system | ✅ Done | CommitEvent with t_start/t_end/stall |

### What needs work

| Gap | Priority | Why |
|-----|----------|-----|
| No trace replay tool | **P0** | Core of the new workflow |
| No Ymir-compatible trace format spec | **P0** | StrikerX3 needs to know what to emit |
| No diff/comparison output | **P0** | The deliverable StrikerX3 actually uses |
| `access_cycles` callback has no Ymir-calibrated implementation | **P1** | Need region table matching Ymir's ConfigureAccessCycles |
| No same-address contention in public `busarb` | **P1** | Internal BusArbiter has it; public API doesn't |
| No round-robin tie-break in public `busarb` | **P1** | Internal BusArbiter has it; public API doesn't |
| No DMA interleaving model | **P2** | Ymir's RunDMA is atomic; traces won't have per-word DMA until later |
| No MA/IF contention model | **P3** | Deferred per StrikerX3's "stall timing" answer |
| Saturnis SH-2 interpreter, devices, BIOS tooling | **FREEZE** | 100% redundant against Ymir |

---

## Step-by-Step Roadmap

### Phase 1: Trace Replay Foundation (P0)

The goal of Phase 1 is to produce a working end-to-end pipeline: trace in → arbiter replay → diff out. Every step here is on the critical path.

**Execution order note (to get an end-to-end proof earlier):** implement **1.1 → 1.2 → 1.3 → 1.6** first, then add **1.4 / 1.5** model refinements. The numbering stays grouped by topic, but the recommended coding order is pipeline-first, refinement-second.

---

#### Step 1.1 — Define the Ymir trace record format (no raw `stall` field yet)

**What:** Specify a minimal JSONL schema for bus access records that Ymir can emit.

**Why:** StrikerX3 needs an unambiguous spec for what to log. The format must be simple enough that adding it to Ymir is a small patch, and rich enough that the arbiter can replay it faithfully **without inventing a fake `stall` value that Ymir does not currently compute**.

**Deliverable:** A document `docs/trace_format.md` specifying a **provisional default schema (recommended)** and an explicit note that the final emission style (per-attempt vs per-successful-access) should be confirmed with StrikerX3 before the format is locked.

**Recommended Phase 1 schema (per-successful-access records, no raw `stall`):**

```jsonl
{"seq":1,"master":"MSH2","tick_first_attempt":1042,"tick_complete":1044,"addr":"0x06004000","size":4,"rw":"R","kind":"ifetch","service_cycles":2,"retries":0}
{"seq":2,"master":"SSH2","tick_first_attempt":1032,"tick_complete":1044,"addr":"0x05FE0098","size":4,"rw":"R","kind":"mmio_read","service_cycles":4,"retries":3}
{"seq":3,"master":"DMA","tick_first_attempt":1050,"tick_complete":1052,"addr":"0x06010000","size":4,"rw":"W","kind":"write","service_cycles":2,"retries":0}
```

**Fields (recommended per-successful-access schema):**

| Field | Type | Description |
|-------|------|-------------|
| `seq` | uint64 | Monotonic trace emission sequence number (required for deterministic replay ordering when multiple records share the same completion tick) |
| `master` | string | `"MSH2"`, `"SSH2"`, or `"DMA"` |
| `tick_first_attempt` | uint64 | Ymir cycle counter when this access first attempted to use the bus (captured once and preserved across retries) |
| `tick_complete` | uint64 | Ymir cycle counter when this access finally completed and was emitted |
| `addr` | hex string | Physical address (27-bit Saturn address space) |
| `size` | uint8 | 1, 2, or 4 bytes |
| `rw` | string | `"R"` or `"W"` |
| `kind` | string | `"ifetch"`, `"read"`, `"write"`, `"mmio_read"`, `"mmio_write"` |
| `service_cycles` | uint32 | Ymir's `AccessCycles()` result for this access (per-attempt service duration, not a contention stall) |
| `retries` | uint32 | Number of blocked retries before success (`0` means completed on first attempt) |

**Alternative (if StrikerX3 prefers lower patch complexity):**
- **Per-attempt records** (duplicates on retries), using a single `tick` field and omitting `retries`.
- The replay tool can derive retries by grouping repeated attempts.
- This is noisier but requires less state tracking in Ymir.

**Important timing semantics (must be explicit in `trace_format.md`):**
- `service_cycles` is **not** a stall. It is the per-attempt service duration returned by `AccessCycles()`.
- `retries` is the count of blocked attempts before the final success.
- `ymir_effective_total` and `ymir_effective_wait` used in reports are **derived values** (not raw fields).
- `retries * service_cycles` is a **comparison proxy**, not necessarily exact elapsed wait, because retry timing is influenced by SH-2 scheduler/step cadence (e.g., sync windows), not only per-attempt service duration.
- `tick_first_attempt` + `tick_complete` exist so the tool can compute actual elapsed timing without baking in retry-cadence assumptions.

**Known Ymir behavior to document up front:**
- Current Ymir byte-sized loads/stores may skip `IsBusWait()` in some handlers.
- As a result, byte accesses may always emit `retries = 0` even when the offline arbiter predicts contention.
- The replay/diff tool must classify those deltas as a **known Ymir wait-model gap**, not an arbiter error.

**Acceptance criteria:**
- [ ] `docs/trace_format.md` exists and is self-contained
- [ ] Format is valid JSONL (one JSON object per line)
- [ ] Every field has explicit type, range, and semantics
- [ ] Document defines whether Phase 1 trace emission is **per-successful-access** (recommended default) or **per-attempt** (alternate mode), and names the exact schema for the chosen mode
- [ ] Document explains `service_cycles` and `retries` semantics and explicitly states that there is **no raw `stall` field** in the Phase 1 schema
- [ ] Document explains `tick_first_attempt` vs `tick_complete` semantics (or, if omitted in a light mode, documents the approximation limits)
- [ ] Document explains that `seq` is the authoritative replay ordering field when completion ticks are equal
- [ ] Document explains retry timing is influenced by SH-2/scheduler stepping cadence, so `retries * service_cycles` is a proxy metric unless elapsed ticks are used
- [ ] Document includes a “Known Ymir behavior” note for byte-sized accesses that skip `IsBusWait()` (and how the diff tool should classify resulting deltas)
- [ ] Emission style and schema semantics are confirmed with StrikerX3 before the format is declared stable

---

**Question to confirm with StrikerX3 before freezing `trace_format.md`:**
- Prefer **per-attempt records** (duplicates on retries) or **per-successful-access records** (`service_cycles` + `retries` + ticks)?
- Are byte-sized loads/stores intentionally skipping `IsBusWait()` in current Ymir handlers for now, or should the trace/integration plan assume those paths may gain bus-wait checks?

---

#### Step 1.2 — Build Ymir-calibrated `access_cycles` callback

**What:** Implement a callback function that returns the same per-region cycle counts as Ymir's `ConfigureAccessCycles()`.

**Why:** The arbiter needs to produce stall values on the same scale as Ymir. Using Ymir's exact region table ensures apples-to-apples comparison.

**Deliverable:** A new file `src/busarb/ymir_timing.cpp` (+ header) containing:

```cpp
namespace busarb {

// Returns read or write cycles for the given Saturn physical address,
// matching Ymir's ConfigureAccessCycles() region table.
uint32_t ymir_access_cycles(void* ctx, uint32_t addr, bool is_write, uint8_t size_bytes);

} // namespace busarb
```

**Region table to implement** (from Ymir source):

| Address Range | Read | Write |
|--------------|------|-------|
| 0x000'0000–0x00F'FFFF (BIOS ROM) | 2 | 2 |
| 0x010'0000–0x017'FFFF (SMPC) | 4 | 2 |
| 0x018'0000–0x01F'FFFF (Backup RAM) | 2 | 2 |
| 0x020'0000–0x02F'FFFF (Low WRAM) | 2 | 2 |
| 0x100'0000–0x1FF'FFFF (MINIT/SINIT) | 4 | 2 |
| 0x200'0000–0x4FF'FFFF (A-Bus CS0/CS1) | 2 | 2 |
| 0x500'0000–0x57F'FFFF (A-Bus dummy) | 8 | 2 |
| 0x580'0000–0x58F'FFFF (CD Block CS2) | 40 | 40 |
| 0x5A0'0000–0x5BF'FFFF (SCSP) | 40 | 2 |
| 0x5C0'0000–0x5C7'FFFF (VDP1 VRAM) | 22 | 2 |
| 0x5C8'0000–0x5CF'FFFF (VDP1 FB) | 22 | 2 |
| 0x5D0'0000–0x5D7'FFFF (VDP1 regs) | 14 | 2 |
| 0x5E0'0000–0x5FB'FFFF (VDP2) | 20 | 2 |
| 0x5FE'0000–0x5FE'FFFF (SCU regs) | 4 | 2 |
| 0x600'0000–0x7FF'FFFF (High WRAM) | 2 | 2 |
| Unmapped (default) | 4 | 2 |

**Acceptance criteria:**
- [ ] `ymir_access_cycles()` returns correct values for every region boundary
- [ ] Unit test covers every row in the table (read and write)
- [ ] Unit test covers boundary addresses (first byte, last byte of each region)
- [ ] Unit test covers unmapped fallback
- [ ] Function is usable as a `TimingCallbacks::access_cycles` callback
- [ ] File compiles as part of the `busarb` target (no Saturnis-internal dependencies)

---

#### Step 1.3 — Build the trace replay tool

**What:** A command-line program that reads a JSONL trace file, replays each record through the `busarb::Arbiter`, and outputs a comparison report.

**Why:** This is the primary deliverable. Everything else exists to support this.

**Important replay semantics:** Phase 1 replay is **comparative replay**, not a closed-loop re-timing simulation. The trace completion ticks remain Ymir's recorded timeline. If the arbiter predicts different timing than Ymir for a record, later records still replay at their recorded `tick_complete`/`seq` values; the tool reports the delta rather than re-timing the rest of the trace.

**Important comparison semantics:** In the recommended schema, Ymir does not emit a raw stall value. The tool derives Ymir timing from `service_cycles`, `retries`, and (preferably) `tick_first_attempt`/`tick_complete`. Tick-based elapsed timing is preferred when available; `retries * service_cycles` is a fallback proxy and should be labeled as such.

**Deliverable:** `tools/trace_replay/trace_replay.cpp` — a standalone executable.

**Behavior:**
1. Read trace file (JSONL, one record per line, format from Step 1.1)
2. Replay records in stable order using `(tick_complete, seq)` (with `seq` as the authoritative tie-break for equal completion ticks)
3. Create `busarb::Arbiter` with `ymir_access_cycles` callback from Step 1.2
4. For each record:
   a. Build a `BusRequest` from the JSON fields
   b. Call `query_wait()` to get the arbiter's stall prediction
   c. Call `commit_grant()` to advance arbiter state
   d. Derive Ymir comparison values from the trace (`service_cycles`, `retries`, and ticks)
      - `ymir_effective_total` (derived)
      - `ymir_effective_wait` (derived; exact when elapsed ticks are available, otherwise proxy)
   e. Compare arbiter `wait_cycles` / derived total timing against Ymir-derived values
   f. Record delta + classification (normal mismatch vs known Ymir wait-model gap)
5. Output summary to stdout:
   - Total records processed
   - Records where arbiter agrees with Ymir (delta == 0)
   - Records where arbiter disagrees (delta != 0)
   - Histogram of deltas by region (and classification)
   - Top 20 largest deltas with full record details
   - Count of deltas classified as known Ymir wait-model gaps (e.g., byte accesses that skip `IsBusWait`)
6. Optionally output annotated trace (original + disambiguated Ymir-derived fields + arbiter fields) as JSONL

**Acceptance criteria:**
- [ ] Reads valid JSONL trace files
- [ ] Replays records in stable `(tick_complete, seq)` order (or the chosen per-attempt equivalent if that mode is selected)
- [ ] Gracefully handles malformed lines (skip with warning, continue)
- [ ] Produces human-readable summary on stdout
- [ ] `--annotated-output <path>` flag writes annotated trace
- [ ] Annotated/comparison output uses disambiguated field names (e.g., `ymir_service_cycles`, `ymir_retries`, `ymir_effective_wait`, `arbiter_wait`, `arbiter_total`)
- [ ] Tool/docs define the exact formulas used to derive Ymir comparison values from trace fields
- [ ] Tool/docs state when Ymir-derived wait is exact (tick-based) vs proxy (`retries * service_cycles`)
- [ ] Byte-access deltas caused by Ymir handlers that skip `IsBusWait()` are classified as known Ymir wait-model gaps, not arbiter errors
- [ ] Tool/help/docs explicitly state replay is **comparative** (no closed-loop re-timing of later records)
- [ ] Builds as a separate CMake executable target, linked only against `busarb`
- [ ] No dependency on `saturnis_core`, SDL2, or any Saturnis internals
- [ ] Test with a hand-crafted 10-record trace file that exercises all three masters and multiple regions

---

#### Step 1.4 — Add same-address contention to public `busarb` API

**What:** Port the `contention_extra()` logic from the internal `BusArbiter` to the public `busarb::Arbiter`.

**Why:** The internal arbiter already computes same-address contention penalties (`same_address_contention = 2` cycles) and tie turnaround costs (`tie_turnaround = 1` cycle). The public API ignores these, which means replay results will be less accurate than the internal model that already exists in this codebase.

**Current public API** (`busarb.cpp`):
```cpp
void Arbiter::commit_grant(const BusRequest &req, uint64_t tick_start) {
    const uint64_t actual_start = std::max(tick_start, bus_free_tick_);
    const uint64_t duration = service_cycles(req);
    bus_free_tick_ = actual_start + duration;  // ← no contention_extra
}
```

**Internal model** (`bus_arbiter.cpp`):
```cpp
const core::Tick latency = base_latency(op) + contention_extra(op, had_tie);
```

Where `contention_extra` adds:
- `same_address_contention` (2 ticks) if current access hits same address as previous
- `tie_turnaround` (1 tick) if multiple masters tied for the bus this cycle

**Deliverable:** Update `busarb::Arbiter` to:
- Track `last_granted_addr_` (like internal `last_addr_`)
- Track `last_granted_master_` (like internal `last_grant_cpu_`)
- Add configurable contention parameters to constructor or a config struct
- Apply extra cycles in `commit_grant()` when conditions match

**Acceptance criteria:**
- [ ] Same-address contention adds penalty when consecutive accesses hit same address
- [ ] Tie turnaround adds penalty when `pick_winner()` was needed (multiple same-tick contenders)
- [ ] Penalties are configurable (default to Ymir-matching values: 2 and 1)
- [ ] Existing tests still pass (contention only triggers when conditions are met)
- [ ] New tests cover: same-address consecutive, different-address consecutive, tie scenario
- [ ] `commit_grant()` signature remains backward-compatible (config set at construction time)

---

#### Step 1.5 — Add round-robin CPU tie-breaking to public `busarb` API

**What:** Port the round-robin CPU tie-break from the internal `BusArbiter::pick_next()` to the public `busarb::Arbiter::pick_winner()`.

**Why:** The internal arbiter alternates between CPU0 and CPU1 when both contend at equal priority. The public API uses a static ordering (SH2_A always beats SH2_B at equal priority), which would produce different stall counts than the internal model for dual-CPU contention scenarios.

**Internal model** (`bus_arbiter.cpp` line 249):
```cpp
if (is_cpu(candidate.cpu_id) && is_cpu(cur.cpu_id) && candidate.cpu_id != cur.cpu_id) {
    const int preferred = (last_grant_cpu_ == 0) ? 1 : 0;
    if (candidate.cpu_id == preferred) {
        best = idx;
    }
    continue;
}
```

**Deliverable:** Update `busarb::Arbiter::pick_winner()` to:
- Track which CPU was last granted (`last_grant_master_`)
- When two CPUs tie at same priority, prefer the one that was NOT last granted
- `commit_grant()` updates `last_grant_master_`

**Acceptance criteria:**
- [ ] CPU tie-break alternates: if SH2_A won last, SH2_B wins next tie
- [ ] DMA still always wins over both CPUs regardless of round-robin state
- [ ] Round-robin only applies to same-priority CPU ties, not DMA-vs-CPU
- [ ] Test: sequence of alternating SH2_A/SH2_B same-tick ties produces alternating winners
- [ ] Test: DMA in the mix still wins regardless of round-robin state

---

#### Step 1.6 — Write the Ymir integration guide

**What:** A self-contained document that StrikerX3 can read to understand what libbusarb is, what the trace format is, and how to add trace emission to Ymir.

**Why:** This is the handoff artifact. It must be clear enough that someone unfamiliar with Saturnis can use it.

**Deliverable:** `docs/ymir_integration_guide.md` containing:

1. **What libbusarb is (Phase 1 scope)** — A standalone timing reference/replay tool used offline. Runtime integration is deferred unless overhead is benchmarked and accepted. Zero performance impact on Ymir's release builds for the Phase 1 workflow.

2. **How to add trace emission to Ymir** — Where to add logging calls (and the small state needed for retries):
   - In SH-2 memory-access handlers (or a centralized helper) — emit one trace record per successful bus access (recommended default) or per attempt (alternate mode)
   - In `SCU::RunDMA()` — emit one trace record per DMA word transfer (or per DMA attempt if/when Ymir models that)
   - For the recommended per-successful-access schema, track `tick_first_attempt` and `retries` across retries using a small persistent SH-2 member (e.g., `m_busAccessFirstTick` / retry counter), then clear on success
   - Each record includes `service_cycles` (`AccessCycles()` result), plus retry/elapsed context (not a raw `stall`)
   - Guarded by a `#ifdef YMIR_BUS_TRACE` or runtime flag so it's zero-cost when disabled

3. **Trace format spec** (reference to `docs/trace_format.md` from Step 1.1)

4. **How to run the replay tool** — Build instructions, command-line usage, output format

5. **How to interpret results** — What a delta of 0 means (Ymir agrees with arbiter under the chosen comparison), what a nonzero delta means (contention the approximate model missed *or* a known Ymir wait-model gap), how to use the per-region histogram to identify which timing zones need tuning, a note that Phase 1 replay is comparative (later trace ticks are not re-timed after a divergence), and a note that `retries * service_cycles` is a proxy metric unless elapsed ticks are used

6. **What the arbiter models** — Single shared bus, three masters (MSH2, SSH2, DMA), per-region access cycles, same-address contention penalty, tie turnaround penalty, round-robin CPU tie-break, DMA priority

7. **What the arbiter does NOT model (yet)** — IF/MA pipeline stage contention, VDP1/VDP2 bus contention with CPUs, SCSP bus contention, variable CS0/CS1 timing, CD Block timing variation, cache line fill bus occupancy beyond single-access cycles, and current Ymir byte-access handlers that may bypass `IsBusWait()`

8. **Known limitations and future directions** — Link to this roadmap's Phase 2/3

**Acceptance criteria:**
- [ ] Document is self-contained (no need to read Saturnis source to use it)
- [ ] Includes exact code snippets showing where Ymir would add trace emission
- [ ] Includes build instructions for the replay tool
- [ ] Includes example trace input and expected output
- [ ] Reviewed for accuracy against Ymir's actual codebase (function names, file paths, and current byte-access `IsBusWait` behavior)
- [ ] Includes a brief implementation note on persistent SH-2 retry tracking needed for `tick_first_attempt`/`retries` in the recommended schema

---

### Phase 1 Exit Criteria

All of the following must be true:

- [ ] `busarb` library builds standalone with no Saturnis-internal dependencies
- [ ] `ymir_access_cycles()` matches Ymir's per-region table with boundary tests
- [ ] `trace_replay` tool reads the chosen JSONL schema (recommended: `service_cycles` + `retries` + ticks), replays through arbiter, and outputs a classified diff report
- [ ] Same-address contention and round-robin tie-break are in the public API
- [ ] Integration guide is written, self-contained, and references correct Ymir code
- [ ] All tests pass under ASAN and UBSAN

**When this is done, hand it to StrikerX3.** Everything after this depends on his feedback.

---

### Phase 2: Calibration and Validation (P1)

Phase 2 begins after StrikerX3 has added trace emission to Ymir and run the replay tool on real game traces. The work here depends on what the diff reports reveal.

---

#### Step 2.1 — Analyze first batch of real-game traces

**What:** Run the replay tool on traces from actual Saturn games and catalog where the arbiter disagrees with Ymir.

**Why:** We don't know yet which contention scenarios actually matter in practice. Real traces will tell us.

**Deliverable:** A report documenting:
- Which memory regions show the largest stall deltas
- Whether dual-CPU contention is common or rare in practice
- Whether DMA contention accounts for significant stall
- Whether same-address penalties are measurable

**Acceptance criteria:**
- [ ] Replay at least 3 different game traces (one lightweight, one heavy, one known-problematic)
- [ ] Categorize deltas by region, master, and access type
- [ ] Identify top 3 contention scenarios by frequency and magnitude

---

#### Step 2.2 — Tune contention parameters against hardware test ROMs

**What:** If celeriyacon's test ROMs (referenced in Ymir's release notes) produce known-correct cycle counts, use them as ground truth to calibrate the arbiter's contention penalties.

**Why:** Test ROMs provide deterministic, controlled scenarios where the correct stall count is known. Real game traces have noise from approximate timing elsewhere.

**Deliverable:**
- Trace recordings from test ROMs run in Ymir
- Replay results showing arbiter vs Ymir vs expected (if known)
- Updated contention parameters if defaults are wrong

**Acceptance criteria:**
- [ ] At least one test ROM trace replayed successfully
- [ ] If contention parameters need adjustment, changes are documented with rationale

---

#### Step 2.3 — Add optional `on_grant` trace hook to public API

**What:** Add an observer callback that fires after each `commit_grant()`, providing the computed stall, actual start tick, and finish tick.

**Why:** Enables richer analysis without modifying the replay tool itself. External tools can hook in for custom visualization, logging, or comparison.

**Deliverable:** New optional callback in `TimingCallbacks` or a separate observer struct:

```cpp
struct ArbiterObserver {
    void (*on_grant)(void* ctx, const BusRequest& req,
                     uint64_t actual_start, uint64_t finish,
                     uint32_t wait_cycles, uint32_t service_cycles) = nullptr;
    void* ctx = nullptr;
};
```

**Acceptance criteria:**
- [ ] Observer is optional (null = no-op, zero overhead)
- [ ] Observer fires after every `commit_grant()`
- [ ] Observer receives all computed timing information
- [ ] Does not affect arbiter determinism (pure observation)
- [ ] Test: observer captures expected values for a known sequence

---

### Phase 3: Extended Contention Models (P2)

Phase 3 adds contention scenarios that the initial model doesn't cover. Each step is independent and can be prioritized based on Phase 2 findings.

---

#### Step 3.1 — SCU DMA per-word interleaving model

**What:** Instead of treating DMA as a single atomic access, model each longword transfer as a separate bus access that can interleave with CPU traffic.

**Why:** Ymir's `SCU::RunDMA()` currently runs to completion atomically (the `// HACK: complete all active transfers` comment). When Ymir eventually breaks this into per-word transfers, the arbiter needs to handle the interleaving correctly.

**Prerequisite:** Ymir emits per-word DMA trace records (not one record per entire transfer).

**Deliverable:**
- Replay tool handles DMA records interleaved with CPU records
- Arbiter correctly applies DMA priority per-word, not per-transfer
- Test with synthetic traces showing CPU accesses interleaved between DMA words

**Acceptance criteria:**
- [ ] Synthetic trace with alternating DMA/CPU records produces correct per-access stalls
- [ ] DMA words receive priority over CPU accesses at same tick
- [ ] CPU accesses between DMA words are correctly delayed by DMA bus occupancy

---

#### Step 3.2 — VDP bus contention model

**What:** Model the bus occupancy caused by VDP1/VDP2 VRAM access during active display.

**Why:** Ymir has TODO comments on VDP1 VRAM and framebuffer timing being variable. CPU accesses to VDP regions during active display should incur additional stall from VDP's own bus usage.

**Prerequisite:** Understanding of VDP bus access patterns (likely requires StrikerX3's input or hardware documentation).

**Deliverable:**
- New `BusMasterId` values: `VDP1`, `VDP2` (or a single `VDP` if they share a bus)
- Arbiter handles VDP as a bus master with its own access pattern
- Trace format extended to include VDP bus events

**Acceptance criteria:**
- [ ] VDP bus events can be replayed
- [ ] CPU accesses to VDP regions during VDP-active periods show increased stall
- [ ] Model matches Ymir's TODO annotations for VDP1 VRAM contention

---

#### Step 3.3 — IF/MA stall timing model (outcome-based)

**What:** Per StrikerX3's clarification ("stall timing is more important"), implement a formula that produces correct stall cycle counts for IF/MA contention without modeling the pipeline stages explicitly.

**Why:** This is the eventual accuracy target, but it requires pipeline stage information that Ymir doesn't currently emit. StrikerX3's Issue #41 describes this as the "next step" after per-region timing.

**Prerequisite:** StrikerX3 has implemented pipeline stage tracking in Ymir's SH-2 interpreter and can emit which stage (IF vs MA) caused a bus access.

**Deliverable:**
- Extended `BusRequest` or trace format with `stage` field (`"IF"` or `"MA"`)
- Arbiter applies additional stall when IF and MA from the same CPU contend for the bus in the same cycle
- Formula calibrated against known pipeline behavior from SH7604 manual

**Acceptance criteria:**
- [ ] Trace records with `stage` field replay correctly
- [ ] IF+MA same-cycle contention produces correct additional stall
- [ ] Results match SH7604 manual timing tables for known instruction sequences
- [ ] Model is a timing formula, not a pipeline simulator (per StrikerX3's preference)

---

### Frozen Work (Do Not Prioritize)

The following items exist in the Saturnis codebase but are explicitly frozen. They should not receive any development effort until Phase 1 is complete and StrikerX3 has provided feedback.

| Item | Why Frozen |
|------|-----------|
| SH-2 interpreter (`sh2_core.cpp`, `sh2_decode.cpp`) | Ymir has a complete, optimized SH-2 with full ISA, cache, DMAC, FRT, WDT, DIVU. Every line duplicates existing work. |
| ISA completeness batches (`BSR`, `JMP @Rm`, etc.) | Same reason. Ymir decodes the full ISA. |
| Device models (`devices.cpp`, DeviceHub) | Ymir has full SMPC, SCU, VDP1, VDP2, SCSP, CD Block. |
| BIOS bring-up / forward-progress tracking | Ymir boots real games at >90% compatibility. |
| ScriptedCPU / emulator.cpp orchestration | Only relevant if building a full emulator, which is not the deliverable. |
| SDL2 window / platform code | Not relevant to a command-line reference tool. |
| Internal `BusArbiter` (Saturnis-side) | Keep it compiling for test coverage, but don't extend it. New features go into the public `busarb` API. |
| `x86_microkernel` option | Not relevant. |

### What to do with frozen code

Don't delete it. It's useful as test infrastructure and as a reference for the internal arbiter's behavior. But don't spend time extending, refactoring, or debugging it. If a test breaks due to changes in the public `busarb` API, fix the test. Don't add new tests for frozen components.

---

## File-Level Change Plan

### New files to create

| File | Phase | Purpose |
|------|-------|---------|
| `docs/trace_format.md` | 1.1 | Ymir trace record JSONL spec |
| `src/busarb/ymir_timing.cpp` | 1.2 | Ymir-calibrated access_cycles callback |
| `src/busarb/ymir_timing.hpp` | 1.2 | Header for above |
| `tests/test_ymir_timing.cpp` | 1.2 | Region table boundary tests |
| `tools/trace_replay/trace_replay.cpp` | 1.3 | Replay tool main |
| `tools/trace_replay/trace_reader.cpp` | 1.3 | JSONL parser |
| `tools/trace_replay/trace_reader.hpp` | 1.3 | Header for above |
| `tools/trace_replay/diff_report.cpp` | 1.3 | Diff summary output |
| `tools/trace_replay/diff_report.hpp` | 1.3 | Header for above |
| `tests/fixtures/traces/minimal_trace.jsonl` | 1.3 | Test fixture |
| `docs/ymir_integration_guide.md` | 1.6 | Handoff document for StrikerX3 |

### Files to modify

| File | Phase | Change |
|------|-------|--------|
| `src/busarb/busarb.cpp` | 1.4, 1.5 | Add contention tracking and round-robin |
| `include/busarb/busarb.hpp` | 1.4, 1.5 | Add config struct, observer hook |
| `tests/test_busarb.cpp` | 1.4, 1.5 | New contention and round-robin tests |
| `CMakeLists.txt` | 1.2, 1.3 | New targets: `ymir_timing_tests`, `trace_replay` |

### Files to leave alone

Everything under `src/cpu/`, `src/core/emulator.*`, `src/dev/`, `src/mem/`, `src/platform/`, `tests/test_kernel.cpp`, `tests/test_trace_regression.cpp`, `tools/bios_metrics/`.

---

## CMake Target Plan

After Phase 1, the build should have:

```cmake
# Standalone arbiter library (no Saturnis dependencies)
add_library(busarb STATIC
    src/busarb/busarb.cpp
    src/busarb/ymir_timing.cpp    # NEW: Ymir-calibrated region table
)
target_include_directories(busarb PUBLIC include)

# Replay tool (depends only on busarb + a JSON parser)
add_executable(trace_replay
    tools/trace_replay/trace_replay.cpp
    tools/trace_replay/trace_reader.cpp
    tools/trace_replay/diff_report.cpp
)
target_link_libraries(trace_replay PRIVATE busarb)

# Tests
add_executable(busarb_tests tests/test_busarb.cpp)
target_link_libraries(busarb_tests PRIVATE busarb)

add_executable(ymir_timing_tests tests/test_ymir_timing.cpp)
target_link_libraries(ymir_timing_tests PRIVATE busarb)

# Existing Saturnis targets (keep compiling, don't extend)
add_library(saturnis_core ...)    # unchanged
add_executable(saturnemu ...)     # unchanged
```

---

## JSON Parsing Strategy

The trace replay tool needs to parse JSONL. Options:

1. **nlohmann/json** — Header-only, widely used, easy to integrate. Adds ~1MB to compile time but zero runtime dependencies.
2. **Hand-rolled minimal parser** — The trace format is trivially simple (flat objects, no nesting, fixed field set). A ~100-line parser would suffice and avoids external dependencies.
3. **RapidJSON** — Faster than nlohmann but more complex API.

**Recommendation:** Start with a hand-rolled parser. The format is simple enough. If it becomes a maintenance burden, switch to nlohmann/json. Avoid adding a dependency that StrikerX3 would need to install to build the tool.

---

## Risk Register

| Risk | Mitigation |
|------|-----------|
| StrikerX3 doesn't want JSONL format | Ask him early. JSONL is simplest but he might prefer binary/CSV. Format is isolated in trace_reader.cpp. |
| Ymir's `tick` counter doesn't map cleanly to `now_tick` | Document the mapping in the integration guide. If Ymir uses fractional cycles, define rounding. |
| `retries * service_cycles` overstates/understates elapsed wait in some cases | Use `tick_first_attempt` + `tick_complete` in the recommended schema; label retry-product comparisons as proxy metrics in reports. |
| Byte accesses skip `IsBusWait()` in current Ymir handlers, producing false-looking deltas | Classify these as known Ymir wait-model gaps in the diff tool and call them out explicitly in docs/integration guide. |
| Real game traces are too large for the replay tool | Add streaming mode (process line-by-line, don't load entire file). |
| Contention parameters don't match hardware | Phase 2 calibration addresses this. Ship Phase 1 with Ymir-matching defaults. |
| StrikerX3 wants runtime integration after all | The public `busarb` API is already designed for it (`query_wait` / `commit_grant` map to `IsBusWait`). The replay tool is a bonus, not a replacement. |
| Scope creep back into SH-2 / emulator work | This roadmap explicitly freezes it. Reference this document when tempted. |

---

## Definition of Done (Phase 1 Handoff)

StrikerX3 can:

1. Read the integration guide and understand what to do
2. Add a small trace-emission patch to Ymir's SH-2 memory access path (including retry tracking if using the recommended per-successful-access schema)
3. Run a game for a few seconds with tracing enabled
4. Feed the resulting trace file to `trace_replay`
5. Get a diff report showing where Ymir's approximate timing diverges from the arbiter's model
6. Use the report to identify which regions or scenarios need tuning

If all six of those work, Phase 1 is complete.
