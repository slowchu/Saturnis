# Saturnis / Ymir Timing Investigation TODO (Post P0-A, Revised)

## Completed

| Task | Outcome |
|------|---------|
| P0-C | Always-on dataset hygiene summary implemented |
| P0-B | Timing decomposition (base_latency / contention_stall / total_predicted) implemented |
| P0-A | **SSH2 +1 best explained by slave-mode arbitration overhead** (confirmed on WRAM-H-heavy workload; see p0a_conclusion.md) |
| Audit | Timing table parity verified, cache symmetry confirmed, address range bug found and fixed |

## Key Finding

SSH2 +1 normalized timing delta best explained by SH7604 slave-mode bus arbitration overhead (confirmed on WRAM-H-heavy workload).
Evidence: 650× mismatch rate between SSH2 (53.6%) and MSH2 (0.08%) on same WRAM-H region.
Timing table and cache model verified symmetric. See p0a_conclusion.md for full writeup.

---

## Execution Order

### 1. Binary Trace Reader

**Priority:** Highest — unblocks everything else.

**Goal:** Read 48-byte binary trace format directly in `trace_replay`, eliminating the JSONL conversion step.

**Requirements:**
- Dual-path: keep JSONL support for debug/inspectability, add binary as fast path
- Streaming mode (don't load entire file into memory)
- Binary header/schema version validation
- Endianness check
- Record-size sanity check (48 bytes)
- Graceful error on truncated file
- **Acceptance test:** binary replay produces identical summary counts/classifications as JSONL replay on the same trace

**Why now:** 208M-record trace = 960MB binary, ~50GB JSONL. Current pipeline cannot handle real-sized datasets. Every subsequent task depends on being able to iterate quickly on large captures.

### 2. MA/IF Contention Breakdown + Dataset Hygiene+

**Priority:** High — low risk, high signal, directly answers StrikerX3's MA/IF contention question.

**Goal:** Cross-tabulate analysis by access kind (IFetch vs Read vs Write).

**New summary fields:**
- `normalized_mismatch_by_master_region_access_kind` (master × region × access_kind cross-tab)
- Mismatch rate AND count (not just raw count)
- **Included sample size (N) per bucket** — sparse buckets must not look significant
- Optional: p90/p99 normalized delta by access_kind

**Dataset hygiene+ extension:**
- `included_master_region_distribution` (master × region counts)
- `included_access_kind_distribution`
- Makes every replay self-auditing and prevents misleading conclusions from skewed slices

**Why:** The trace already records IFetch vs Read/Write. This is a reporting change, not architectural. Directly answers "is IF vs data behaving differently?"

### 3. Size-Aware Timing (if cheap)

**Priority:** Medium-high — evaluate scope before committing.

**Goal:** Plumb `size_bytes` into `ymir_access_cycles()` and account for bus-width conversion (32-bit read from 16-bit bus = 2× base cost).

**Decision gate:** If implementation is mostly "plumb size into region service calculation," do it here. If it affects assumptions in classifiers or normalization logic, defer to after cache work.

**Why:** Improves absolute prediction accuracy for longword accesses. Matters for DMA burst analysis later.

### 4. Cache-Aware Analysis (Phased)

**Priority:** Medium — important for interpreting secondary mismatch populations, but scope carefully.

**Phase 1 (cheap):** Observed-cycle bucketing
- Classify observed accesses as likely hit/miss based on `serviceCycles` in trace (1 = hit, 8 = miss for WRAM-H)
- Report deltas per bucket
- No cache emulation, no prediction — purely descriptive

**Phase 2 (medium):** Simplified cache prediction
- First true predictor work: model enough to distinguish common hit/miss behavior in Saturnis's predictor
- Improve normalization so secondary populations become interpretable

**Phase 3 (optional):** Fuller cache model
- Only if needed for remaining mismatch populations after Phase 1-2

**Why:** Current absolute deltas are noisy (cache hit = -1, miss = +6 against flat prediction of 2). Phased approach keeps Saturnis as a calibration tool, not a second emulator.

### 5. DMA Stress Testing

**Priority:** Medium-low — current results encouraging but not comprehensive.

**Goal:** Capture and analyze traces during heavy DMA activity.

**Target scenarios:**
- FMV playback
- Loading screens
- SCU-heavy scenes
- Longer capture windows

**Success criteria (define before capture):**
- Minimum DMA event count: >1M
- Mismatch rate threshold: TBD (baseline from current 0% on 813K)
- Delta distribution threshold: p90/p99 values TBD
- Region/master breakdown required for every analysis run
- Pass/fail determined before replay, not after

**Why:** 0 mismatches on 813K DMA accesses is a positive signal but needs broader coverage.

### 6. Access Ordering Analysis

**Priority:** Low — hardest task, different analysis mode entirely.

**Prerequisites before implementation:**
- Design doc with explicit invariants
- Synthetic test corpus for validation
- Clear definition of what "correct ordering" means in Saturnis's model

**Goal:** Check whether accesses from MSH2/SSH2/DMA arrive at the bus in correct relative sequence, not just correct timing.

**Why:** StrikerX3 asked about "ordering under contention." Current Saturnis only compares per-event timing, not interleaved stream ordering.

---

## Traces Collected

| Trace | Records | SSH2% | Key Regions | File |
|-------|---------|-------|-------------|------|
| Game trace (original) | 200K | 41% | WRAM-H, on-chip regs | (previous session) |
| BIOS boot (no disc) | 5M | 0% | BIOS ROM, WRAM-H, A-Bus | trace_boot_small.bin |
| Panzer Dragoon boot | 20M | 0% | BIOS ROM, WRAM-H, A-Bus | trace_pd_boot.bin |
| Panzer Dragoon gameplay | 20M | 38% | WRAM-H, VDP2, A-Bus, BIOS ROM | trace_pd_gameplay.bin |

### Provenance

| Artifact | Value |
|----------|-------|
| Saturnis commit | TODO: pin after pushing fixes |
| Ymir trace fork commit | TODO: pin current HEAD |
| Trace binary format | 48-byte records, 8-byte header |
| Summary schema version | v3 |
| Replay tool | `trace_replay` (Saturnis) |
| Game tested | Panzer Dragoon Saga (AZEL_1.chd) |
| BIOS | Auto-detected IPL ROM via Ymir |

**Rule:** Update provenance pins whenever commits are pushed or trace format changes.

---

## Ymir Fork Changes

| Change | File | Purpose |
|--------|------|---------|
| Auto-start capture | bus_trace.cpp | `YMIR_BUS_TRACE_AUTO_START=1` for boot traces |

Fork remains observation-only. No timing model changes.

---

## Research Hygiene Rules

- Do not interpret deltas without dataset hygiene summary visible
- Prefer trusted-class subsets first (W/L, known-gap exclusions)
- Keep outputs deterministic and diff-friendly
- Separate measurement from hypothesis conclusions
- Never modify Ymir timing model in the fork
- **New:** Validate binary reader parity against JSONL before switching
- **New:** Cross-tabs before conclusions — always check master × region × access_kind before attributing patterns
