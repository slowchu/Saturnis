# Saturnis / Ymir Timing Investigation TODO v3

## Status Summary

| Task | Status | Outcome |
|------|--------|---------|
| P0-C | ✅ Complete | Always-on dataset hygiene summary implemented |
| P0-B | 🔄 In progress | Phase 1 partial landed: model-comparison is now opt-in via `--include-model-comparison`, default replay is trace-only, and default annotated output omits model-derived fields. Remaining: move model metrics under a dedicated `model_comparison` JSON section and complete metric rename migration |
| P0-A | ❌ **RETRACTED** | Prior SSH2 +1 conclusion traced to Saturnis normalization artifact. Raw trace inspection confirmed both CPUs produce identical records (`svc=1, elapsed=1, wait=0`). The arbiter model generated the asymmetry, not the trace. See `p0a_conclusion_retracted.md` and `p0a_postmortem.md` |
| Audit | ✅ Complete | Timing table parity verified, cache symmetry confirmed, address range bug found+fixed |
| Binary reader | ✅ Complete | Struct layout fixed, parity validated. Seq-tracking memory cap added (2M). Full streaming refactor still needed |
| MA/IF cross-tab | ✅ Complete (infrastructure) | Cross-tab by master × region × access_kind works correctly. However, the metric it computes over (`normalized_delta_wait`) is arbiter-derived and must be replaced with trace-grounded metrics |

---

## Key Lesson

**Saturnis's "normalized mismatch" metric was not a measurement — it was a model disagreement score.**

The `normalized_delta_wait = arbiter_predicted_wait - ymir_wait` formula blends an unvalidated arbiter prediction with a trace-derived value. The arbiter predicted contention for SSH2 but not MSH2 due to access frequency imbalance in its `bus_free_tick` timeline. This produced a 70% "mismatch rate" for SSH2 that looked like a hardware signal but was entirely model-generated.

Raw trace data shows: both CPUs have `serviceCycles=1, elapsed=1, wait=0` on WRAM-H cache-hit instruction fetches. Ymir treats them identically. There is no observed +1 penalty in the trace.

**Rule going forward:** No finding may be reported as "observed" or "measured" unless it can be demonstrated using only trace-recorded fields (`serviceCycles`, `elapsed`, `wait`, `tick_first_attempt`, `tick_complete`, `master`, `addr`, `size`, `kind`). Any finding that requires the arbiter model must be labeled as a hypothesis.

---

## What Saturnis CAN Say Today (Validated Facts)

These are grounded in raw trace data and verified source inspection:

1. Ymir does not model bus contention on array-backed regions (`IsBusWait()` returns false for WRAM-H/WRAM-L). In analyzed traces, `wait=0` is observed for all accesses to these regions. (This does not guarantee `wait=0` in all possible workloads — only that it holds across the ~100M+ records analyzed so far spanning boot, cutscene, and combat scenarios.)
2. Ymir's timing table matches Saturnis's `ymir_access_cycles()` for all 16 address ranges (verified by source comparison of `ConfigureAccessCycles()`).
3. Both CPUs use identical cache template parameters (`enableSH2Cache`). Cache model is symmetric.
4. WRAM-H instruction fetches are predominantly cache hits (`serviceCycles=1`) for both CPUs during gameplay/combat workloads.
5. Trace instrumentation (`TraceBusAccessComplete`) is symmetric — called identically for both CPUs.
6. Binary trace format is correct: 48-byte records, 8-byte header, struct layout verified and parity-tested.

---

## Execution Order

### Progress Notes (rolling)

- [x] **Phase 1 (partial): opt-in model comparison gate implemented**
  - Added `--include-model-comparison` flag to `trace_replay`; default mode is trace-only.
  - Default annotated output now excludes arbiter/model-derived fields.
  - Default stdout now explicitly reports `Model comparison: DISABLED (trace-only mode)`.
  - Remaining follow-up: emit a dedicated `model_comparison` top-level section and finish deprecated metric name migration.

### Phase 0: Stop the Bleeding (Documentation Correction)

**Priority:** Immediate — do before any code changes.

**Status:** ✅ COMPLETE (this document + retraction doc)

**Tasks:**

1. ~~Mark `p0a_conclusion.md` and `p0a_conclusion_v2.md` as retracted/superseded~~
   - Created `p0a_conclusion_retracted.md` with claim-by-claim retraction
   - Root cause analysis documenting how the false positive occurred

2. ~~Replace "Key Finding" in TODO with retraction notice~~
   - This document (v3) replaces `saturnis_todo_revised.md`
   - "Key Lesson" section above replaces the old "Key Finding"

3. ~~Do not send any version of the P0-A conclusion to StrikerX3~~
   - Documented in retraction notice header

**Deliverables:**
- `p0a_conclusion_retracted.md` — full retraction with claim-by-claim analysis
- `p0a_postmortem.md` — internal investigation of what the +1 actually is
- `saturnis_todo_v3.md` — this document, replacing all previous TODO versions

---

### Phase 1: Separate Trace-Fact from Model-Comparison in Output

**Priority:** Highest — this is the architectural fix that prevents the P0-A mistake from recurring.

**Goal:** Split Saturnis's replay output into two clearly separated analysis modes so that trace observations can never again be confused with model predictions.

**Current problem (detailed):**

The replay loop currently computes everything in a single pass and intermixes trace-derived values with arbiter-derived values in the same output structures:

```cpp
// These are trace-derived (GOOD):
r.ymir_service_cycles = record.service_cycles;
r.ymir_elapsed = record.tick_complete - record.tick_first_attempt;
r.ymir_wait = (ymir_elapsed > ymir_service_cycles) ? (ymir_elapsed - ymir_service_cycles) : 0;

// These are model-derived (MUST BE SEPARATED):
r.arbiter_predicted_wait = estimate_local_wait_cycles(...);
r.contention_stall = r.arbiter_predicted_wait;
r.normalized_delta_wait = arbiter_predicted_wait - ymir_wait;  // THE PROBLEM LINE
```

The `normalized_delta_wait` is model-derived but gets used as the primary classification metric (`normalized_mismatch_count`, `normalized_mismatch_by_master_region_access_kind`, etc.), making model output look like measurement.

**Implementation plan:**

#### A. Define two output sections in the summary JSON

```json
{
  "trace_observed": {
    "source": "TRACE_ONLY",
    "description": "All metrics derived exclusively from trace-recorded fields",
    "...": "..."
  },
  "model_comparison": {
    "source": "MODEL_COMPARISON",
    "description": "Metrics involving Saturnis arbiter predictions — HYPOTHESIS ONLY",
    "...": "..."
  }
}
```

#### B. Trace-Observed section contents

These use ONLY fields from the binary trace record (`serviceCycles`, `elapsed`, `wait`, `master`, `addr`, `size`, `kind`, `tick_first_attempt`, `tick_complete`, `retries`):

**Per-bucket distributions** (bucket = master × region × access_kind, and optionally × cache_bucket × size):

| Metric | Formula | What It Shows |
|--------|---------|---------------|
| `observed_service_cycles_distribution` | Raw `serviceCycles` values, counted per bucket | Cache hit/miss/through signature |
| `observed_elapsed_distribution` | `tick_complete - tick_first_attempt`, per bucket with p50/p90/p99 | Total access latency as Ymir sees it |
| `observed_wait_distribution` | `max(0, elapsed - serviceCycles)`, per bucket with p50/p90/p99 and nonzero count | Wait/contention as Ymir sees it |
| `observed_wait_nonzero_count` | Count of records where `elapsed > serviceCycles`, per bucket | How often Ymir models any wait at all |
| `observed_wait_nonzero_rate` | `observed_wait_nonzero_count / N`, per bucket | Rate of Ymir-observed waits |
| `observed_retries_distribution` | Raw `retries` values, per bucket with p50/p90/p99 | Retry behavior |
| `N` | Sample size per bucket | Required for all buckets — sparse buckets must not look significant |

**Cross-CPU symmetry checks** (the analysis we SHOULD have done from the start):

For each region × access_kind × cache_bucket combination where both MSH2 and SSH2 have N > 100:

| Check | Method | What It Detects |
|-------|--------|-----------------|
| `service_cycles_symmetry` | Compare `serviceCycles` distribution between MSH2 and SSH2 | Different cache behavior between CPUs |
| `elapsed_symmetry` | Compare `elapsed` distribution between MSH2 and SSH2 | Different total latency (would indicate real arbitration if present) |
| `wait_symmetry` | Compare `wait` distribution between MSH2 and SSH2 | Different wait/contention modeling per CPU |
| `wait_nonzero_rate_symmetry` | Compare nonzero wait rates | One CPU being penalized more than the other |

Report: for each check, show both distributions side by side with N, mean, p50, p90, p99. Flag any pair where distributions differ by more than a configurable threshold. **If Ymir treats both CPUs identically (as current data suggests), these symmetry checks will all pass — and that IS the finding.**

**Master distribution and access pattern summary:**

| Metric | What It Shows |
|--------|---------------|
| `master_distribution` | How many accesses per master (already exists in P0-C) |
| `region_distribution` | How many accesses per region (already exists) |
| `access_kind_distribution` | ifetch/read/write/mmio_read/mmio_write counts (already exists) |
| `master_region_access_kind_distribution` | Full cross-tab with N per bucket (already exists, keep as-is) |
| `cache_bucket_distribution` | New: counts by observed serviceCycles value per master × region × access_kind |

#### C. Model-Comparison section contents

These use the arbiter and are clearly labeled as hypothesis/simulation:

| Metric | Formula | Label |
|--------|---------|-------|
| `model_predicted_wait` | `estimate_local_wait_cycles()` output | `SOURCE: MODEL_COMPARISON` |
| `model_predicted_total` | `arbiter_predicted_wait + arbiter_predicted_service` | `SOURCE: MODEL_COMPARISON` |
| `model_vs_trace_wait_delta` | `arbiter_predicted_wait - ymir_wait` | `SOURCE: MODEL_COMPARISON` |
| `model_vs_trace_total_delta` | `arbiter_predicted_total - ymir_elapsed` | `SOURCE: MODEL_COMPARISON` |
| `hypothesis_mismatch_count` | Count where `model_vs_trace_wait_delta != 0` | `SOURCE: MODEL_COMPARISON` |
| `hypothesis_mismatch_by_bucket` | Per master × region × access_kind | `SOURCE: MODEL_COMPARISON` |

**These metrics should be opt-in**, not computed by default. Add a flag: `--include-model-comparison` or similar. Default output is trace-observed only. When model comparison is disabled (the default):
- The `model_comparison` section is **omitted entirely** from the JSON output (not present as empty/null)
- Stdout prints: `Model comparison: DISABLED (trace-only mode)`
- No arbiter state is allocated or updated during the replay loop

This ensures that default output cannot be misread as containing model-validated findings, even by a casual reader who doesn't check source labels.

#### D. Deprecated metric names

The following metric names must not appear in the default output path:

| Old Name | Problem | Replacement |
|----------|---------|-------------|
| `normalized_mismatch_count` | Sounds observational, is model-derived | `hypothesis_mismatch_count` (model section) or `observed_wait_nonzero_count` (trace section) |
| `normalized_delta_wait` | Sounds observational, is model-derived | `model_vs_trace_wait_delta` (model section) |
| `normalized_delta_total` | Same | `model_vs_trace_total_delta` (model section) |
| `normalized_agreement_count` | Same | Remove or move to model section |
| `contention_stall` | Implies observed contention; is arbiter prediction | `model_predicted_wait` (model section) |
| `base_latency` | Ambiguous — is this observed or predicted? | `observed_service_cycles` (trace) or `model_predicted_service` (model) |

#### E. Code changes required

1. **`ReplayResult` struct:** Split into `TraceObservation` (trace fields only) and `ModelComparison` (arbiter fields). The `ModelComparison` struct is optional and only populated when `--include-model-comparison` is set.

2. **Replay loop:** Compute trace-observed metrics unconditionally. Compute arbiter predictions only when model comparison is requested.

3. **Summary output:** Two top-level sections with `source` labels. Default mode omits the model comparison section entirely.

4. **Stdout output:** Same separation. Trace-observed summary first, model comparison summary second (if requested).

5. **Classification logic:** The current `known_ymir_wait_model_gap` and `mismatch` classifications depend on `normalized_delta_wait`. These must move to the model comparison section. The trace-observed section should classify based on observed fields only (e.g., "wait_nonzero", "high_elapsed", "anomalous_service_cycles").

**Estimated scope:** Medium — mostly restructuring output, not rewriting analysis logic. The replay loop already computes all the trace-derived values; they just need to be surfaced independently.

---

### Phase 2: Rename Metrics and Add Source Labels

**Priority:** High — coupled with Phase 1 but can be done incrementally.

**Goal:** Make it impossible to confuse a model prediction with a trace observation in any Saturnis output.

**Implementation:**

1. Every metric in both JSON and stdout output carries a `source` field:
   - `TRACE_ONLY` — derived exclusively from trace-recorded fields
   - `MODEL_COMPARISON` — involves Saturnis arbiter or other unvalidated model
   - `HYBRID` — trace-derived but with model-informed bucketing (e.g., cache-aware predictions in Phase 3)

2. Every cross-tab bucket includes `N` (sample size) prominently. Buckets with N < 100 are flagged as `LOW_SAMPLE`.

3. Summary JSON schema version bumps to v4. Document the schema change.

**Why this is separate from Phase 1:** Phase 1 is the structural split. Phase 2 is the labeling discipline. They can be implemented together but the concepts are distinct — you could have the split without labels or labels without the split, but you need both.

---

### Phase 3: Cache-Aware Bucketing (Observed)

**Priority:** High — this is where real analytical value returns.

**Goal:** Classify every trace record by its observed cache state and report all metrics per cache bucket. This separates cache effects from everything else, which is essential for interpreting any timing analysis on cached memory regions.

**Observed cache buckets for WRAM-H (0x06xxxxxx):**

| serviceCycles | Classification | Expected Meaning |
|---------------|---------------|------------------|
| 1 | `cache_hit` | Instruction/data cache hit, no external bus access |
| 2 | `uncached_or_through` | Probable cache-through (0x26xxxxxx partition) or uncached access. **Note:** `serviceCycles==2` on WRAM-H is a behavioral bucket, not a guaranteed semantic label — it could also indicate a single-word uncached fetch or other paths that happen to cost 2 cycles. Do not assume cache-through without checking the address partition bits. |
| 4 | `cache_miss_half` | Possible partial cache line fill (2 × 2) — verify if this occurs |
| 8 | `cache_miss_full` | Full cache line fill on 16-bit bus (4 words × 2 cycles) |
| Other | `anomaly` | Unexpected value — flag for investigation |

**For non-WRAM-H regions:**

| Region | Expected serviceCycles | Notes |
|--------|----------------------|-------|
| BIOS ROM | Varies by BSC config | Likely 4-10 cycles depending on BCR1/WCR settings |
| A-Bus CS0/CS1 | Varies by ASR0/ASR1 | SCU-mediated, separate timing model |
| VDP2 | 2 (word) | Fixed timing per VDP2 spec |
| On-chip registers | 1 | Internal bus, no external access |

**Implementation:**

1. Add `cache_bucket` field to `TraceObservation`:
   ```cpp
   enum class CacheBucket : uint8_t {
     CacheHit = 0,          // serviceCycles == 1
     UncachedOrThrough = 1, // serviceCycles == 2 on cached region (behavioral bucket, not semantic guarantee)
     CacheMissFull = 2,     // serviceCycles == 8 on WRAM-H
     Anomaly = 3,           // anything else
     NotApplicable = 4      // non-cacheable region (VDP2, MMIO, etc.)
   };
   ```

2. Add `cache_bucket` as a dimension in the cross-tab: `master × region × access_kind × cache_bucket`

3. Report per cache bucket:
   - N (count)
   - `observed_elapsed` distribution (p50/p90/p99)
   - `observed_wait_nonzero_count` and rate
   - `observed_service_cycles` (should be uniform within bucket — if not, bucketing is wrong)

4. **Symmetry checks per cache bucket:** For each `region × access_kind × cache_bucket` combination, compare MSH2 vs SSH2 `elapsed` and `wait` distributions. This is the analysis that would have caught the P0-A false positive immediately.

**What this enables:**

- If MSH2 and SSH2 show identical `elapsed` and `wait` within the same cache bucket, we confirm Ymir treats both CPUs symmetrically (which is what current raw data shows).
- If a specific cache bucket shows asymmetry, THAT is a real finding — e.g., "SSH2 cache misses have elapsed=10 while MSH2 cache misses have elapsed=8, suggesting Ymir models different miss penalties."
- Cache hit analysis becomes possible: "70% of SSH2 ifetches are cache hits, 5% are cache misses, 25% are cache-through" — this tells us about the workload, not about timing errors.

**What this does NOT do (yet):**

- Does not predict cache hits/misses — only buckets observed values
- Does not simulate cache state — that's Phase 3b (below)

---

### Phase 3b: Cache Predictor (Optional, Deferred)

**Priority:** Medium — only pursue if Phase 3 reveals anomalies worth investigating.

**Goal:** Build a simplified SH7604 instruction cache model to predict hit/miss and compare predictions against observed `serviceCycles`.

**SH7604 instruction cache parameters:**
- 4-way set-associative
- 64 sets (6-bit index)
- 16 bytes per line (4 × 32-bit words, but fetched as 8 × 16-bit over the external bus)
- LRU replacement policy
- Address tag: bits [31:10], index: bits [9:4], word offset: bits [3:2], byte: bits [1:0]

**What this enables:**

- Predict whether each ifetch should hit or miss
- Compare prediction against observed `serviceCycles`
- Disagreements mean Ymir's cache model differs from the SH7604 spec — that IS actionable for StrikerX3

**Why deferred:** The observed bucketing in Phase 3 may be sufficient. If Ymir's cache model matches the SH7604 spec (which is likely, since StrikerX3 presumably implemented it from the manual), the cache predictor will agree with the trace and produce no findings. Only build it if Phase 3 shows anomalous cache behavior.

---

### Phase 4: Size-Aware Timing

**Priority:** Medium — improves absolute prediction accuracy.

**Goal:** Plumb `size_bytes` into timing predictions so that bus-width conversion is accounted for.

**SH7604 bus-width conversion rules:**
- 32-bit (long) read from 16-bit bus (WRAM-H): 2 bus cycles
- 16-bit (word) read from 16-bit bus: 1 bus cycle
- 8-bit (byte) read from 16-bit bus: 1 bus cycle
- Write behavior varies by region

**Implementation:**
- Add `bus_width` to the region table (WRAM-H = 16-bit, BIOS ROM = 16-bit, VDP2 = 16-bit, on-chip regs = 32-bit)
- For cached accesses, size doesn't matter (cache line fill is fixed)
- For uncached/cache-through accesses: `adjusted_cycles = base_cycles × ceil(access_size / bus_width)`
- For cache miss fills: always 4 words × (base_cycles per word) regardless of original access size

**Decision gate:** If this is straightforward (just a multiplier on uncached paths), do it. If it requires changes to the classification logic or interacts with cache bucketing in complex ways, defer to after Phase 3 is validated.

**Why this matters:** The current flat `ymir_access_cycles()` returns 2 for WRAM-H regardless of size. A 32-bit uncached read actually costs 4 cycles (2 × 2). This affects absolute delta calculations for non-cached access paths.

---

### Phase 5: Streaming Replay (Memory Fix)

**Priority:** High — blocks analysis of traces larger than ~20M records.

**Current memory problem (detailed):**

The replay tool has two memory bottlenecks:

1. **`std::set<uint64_t>` for duplicate sequence tracking** — O(N) memory, ~8GB at 100M records. **PARTIALLY FIXED** with 2M cap + disable.

2. **`std::vector<TraceRecord>` holding all records** — each `TraceRecord` contains multiple `std::string` fields (`master`, `addr_text`, `rw`, `kind`, `classification`). At 100M records, this is ~20GB+ from heap-allocated strings alone.

3. **`std::vector<int64_t>` for per-access-kind delta lists** — used for percentile calculations. 20M int64 values per kind = ~640MB. Not the biggest problem but adds up.

**Solution: two-pass streaming architecture**

**Pass 1 (streaming summary):**
- Read records one at a time from the binary file
- Compute all summary statistics incrementally (counts, distributions, cross-tabs)
- Use online algorithms for percentiles (t-digest or sorted reservoir sampling)
- Use enum values instead of strings during accumulation (master_id, region_id, kind_id)
- Convert to strings only during output formatting
- No record vector — each record is processed and discarded
- Memory: O(buckets) ≈ O(1), not O(records)

**Pass 2 (optional, for top-K):**
- Only run if `--top-k` is requested
- Re-read the file and maintain a min-heap of K records by the sort criterion
- Memory: O(K), typically K=20

**Record struct for streaming:**
```cpp
struct StreamRecord {
  uint64_t seq;
  uint64_t tick_first_attempt;
  uint64_t tick_complete;
  uint64_t service_cycles;     // raw from trace
  uint64_t retries;            // raw from trace
  uint32_t addr;
  uint8_t  size;
  uint8_t  master;             // enum, not string
  uint8_t  rw;                 // 0=read, 1=write
  uint8_t  kind;               // enum, not string
  // Derived (computed inline, not stored):
  // elapsed = tick_complete - tick_first_attempt
  // wait = max(0, elapsed - service_cycles)
  // cache_bucket = classify(service_cycles, addr, kind)
  // region = classify(addr)
};
```

No strings. No heap allocation per record. 48 bytes in, 48 bytes processed, discard.

**Accumulator struct:**
```cpp
struct BucketAccumulator {
  uint64_t count = 0;
  uint64_t wait_nonzero_count = 0;
  uint64_t sum_elapsed = 0;
  uint64_t sum_service_cycles = 0;
  uint64_t sum_wait = 0;
  // For percentiles — use reservoir sampling or t-digest
  ReservoirSampler<int64_t> elapsed_sampler;
  ReservoirSampler<int64_t> wait_sampler;
  ReservoirSampler<int64_t> service_cycles_sampler;
};

// Keyed by: (master, region, access_kind, cache_bucket, size)
// Total buckets: 3 masters × 6 regions × 5 kinds × 5 cache_buckets × 3 sizes = 1,350 max
// In practice much fewer (most combinations don't occur)
std::map<BucketKey, BucketAccumulator> accumulators;
```

**Estimated scope:** Medium-large. Requires restructuring the replay loop but the logic is straightforward. The hardest part is the online percentile calculation — reservoir sampling with K=10,000 gives good p90/p99 estimates without storing all values.

**Percentile estimation acceptance criteria:**
- Before switching from exact to approximate percentiles, validate on a known trace (e.g., the 20M PD combat trace where exact values are already computed)
- p90 estimate must be within ±1 of exact value on a 1M+ record validation set
- p99 estimate must be within ±1 of exact value on a 1M+ record validation set
- Document the estimator used (reservoir size, algorithm) in the summary JSON metadata so that changes in percentile values between runs can be attributed to estimator differences vs real data changes
- If an estimated percentile differs from a previously-reported exact percentile, the report must flag this: `"percentile_method": "reservoir_sampling_k10000"` vs `"percentile_method": "exact"`

---

### Phase 6: Re-run Analysis with Trace-Grounded Metrics

**Priority:** High — this is where we find out what Saturnis can actually say.

**Goal:** Re-analyze existing traces (gameplay, combat, boot) using only trace-observed metrics. Answer the questions we thought we'd answered, but correctly this time.

**Questions to answer with trace-only data:**

1. **Does Ymir treat MSH2 and SSH2 differently?**
   - Compare `elapsed` distributions per master within same region × access_kind × cache_bucket
   - Compare `wait` nonzero rates per master within same buckets
   - Compare `serviceCycles` distributions per master (cache hit rates)
   - **Expected answer based on raw dump:** No — both show `svc=1, elapsed=1, wait=0` on cache-hit ifetches. But we need to check ALL cache buckets, not just hits.

2. **What is the cache hit rate per CPU?**
   - Count `serviceCycles=1` vs `serviceCycles=8` per master × access_kind
   - If hit rates differ substantially between CPUs, that's a real workload characterization finding

3. **Are there ANY non-zero wait values in the trace?**
   - Count `wait > 0` across all records
   - If the answer is "effectively zero" for all regions on all masters, that confirms Ymir models no bus contention — which is the real finding for StrikerX3 (stated as a trace observation, not a model prediction)

4. **Where does Ymir model wait?**
   - For regions where `IsBusWait()` returns true (A-Bus, BIOS ROM with external device), check if `wait > 0` appears
   - This tells us which bus contention paths Ymir actually exercises

5. **Are there anomalous serviceCycles values?**
   - Any `serviceCycles` values outside {1, 2, 4, 8} for WRAM-H?
   - Any values that don't match expected cache behavior?
   - These would be genuine timing model findings

6. **DMA behavior:**
   - DMA `elapsed` and `serviceCycles` distributions
   - DMA shows 0% mismatch in the old analysis, but we should verify using trace-observed metrics
   - Does DMA show different `serviceCycles` than CPU accesses to the same regions?

**Traces to re-analyze:**

| Trace | Records | Key Properties | File |
|-------|---------|---------------|------|
| PD gameplay | 20M | Mixed cutscene/gameplay, first trace with SSH2 volume | trace_pd_gameplay.bin |
| PD combat | 20M | Active combat, highest SSH2 data-read count (220K) | trace_pd_combat.bin |
| BIOS boot | 5M | MSH2-only, region diversity | trace_boot_small.bin |

**Deliverable:** New summary JSONs with trace-observed metrics. Compare against old summaries to document what changed and what the trace actually shows vs what the arbiter was claiming.

---

### Phase 7: Rebuild Model Comparison Mode (Optional)

**Priority:** Low — only after trace-observed analysis is complete and validated.

**Goal:** Keep the arbiter as an opt-in hypothesis tool, but fix its known issues and label its output correctly.

**Known arbiter issues to address:**

1. **No cache model:** The arbiter uses `ymir_access_cycles()` which returns flat values. It should use the same cache bucketing as Phase 3 to make its predictions comparable.

2. **Single bus timeline is too simplistic:** Real Saturn has the SCU mediating A-Bus access, DMA channels with their own arbitration, and the SH2's internal bus for on-chip registers. A single `bus_free_tick` conflates all of these.

3. **No validation baseline:** The arbiter has never been tested against known-correct timing data. Without this, its output is speculative.

**If rebuilt, the arbiter should:**
- Use cache-aware service cycle predictions
- Separate bus timelines for CPU bus, A-Bus (SCU-mediated), and internal bus
- Label ALL output as `SOURCE: MODEL_COMPARISON`
- Report confidence levels based on how well the model matches trace observations where they overlap

**This is optional.** The trace-observed analysis (Phases 1-6) may be sufficient for the deliverable to StrikerX3. The arbiter rebuild is only valuable if trace-observed analysis reveals asymmetries that need a contention model to explain.

---

### Phase 8: DMA Stress Testing

**Priority:** Medium-low — current results encouraging but coverage is thin.

**Goal:** Capture and analyze traces during DMA-heavy workloads using trace-observed metrics.

**Target scenarios:**
- FMV playback (CD-ROM DMA → WRAM → VDP2)
- Loading screens (CD-ROM DMA bursts)
- VDP2-heavy scenes (SCU DMA to VDP2 VRAM)

**Success criteria (define BEFORE capture):**
- Minimum DMA event count: >1M
- Report `observed_elapsed` and `observed_service_cycles` distributions for DMA per region
- Report `observed_wait_nonzero_rate` for DMA per region
- Compare DMA timing to CPU timing for same regions
- All metrics must be trace-observed, not model-derived

**Note:** The previous 0% mismatch finding on 813K DMA accesses needs reinterpretation. The "mismatch" was model-derived (`normalized_delta_wait`). We need to re-examine DMA behavior with trace-observed metrics.

---

### Phase 9: Access Ordering Analysis

**Priority:** Low — hardest remaining task, different analysis mode.

**Prerequisites:**
- Design doc with explicit invariants
- Synthetic test corpus for validation
- Clear definition of "correct ordering" that doesn't depend on the arbiter model
- Phase 5 (streaming) must be complete — ordering analysis requires cross-referencing sequential records

**Goal:** Check whether interleaved MSH2/SSH2/DMA access streams maintain physically valid ordering.

**Approach:** Compare access timestamps across masters. If MSH2 access N completes at tick T and SSH2 access M starts at tick T-1, that might indicate an ordering violation (two masters accessing the bus simultaneously). But defining what "valid" means requires understanding Saturn's actual bus arbitration timing, which is what we're trying to measure.

**This is deferred** because it requires either hardware reference data or a validated bus model — neither of which we currently have.

---

## Traces Collected

| Trace | Records | SSH2% | Key Regions | Capture Context | File |
|-------|---------|-------|-------------|-----------------|------|
| Game trace (original) | 200K | 41% | WRAM-H, on-chip regs | Unknown gameplay | (previous session) |
| BIOS boot (no disc) | 5M | 0% | BIOS ROM, WRAM-H, A-Bus | Boot sequence | trace_boot_small.bin |
| PD boot | 20M | 0% | BIOS ROM, WRAM-H, A-Bus | Game boot | trace_pd_boot.bin |
| PD gameplay | 20M | 38% | WRAM-H, VDP2, A-Bus, BIOS ROM | Cutscene + transition | trace_pd_gameplay.bin |
| PD opening cutscene | 20M | 37% | WRAM-H, on-chip regs, A-Bus | Post-new-game cutscene | trace_pd_fmv.bin |
| **PD combat** | **20M** | **40%** | **WRAM-H, BIOS ROM, A-Bus, on-chip regs** | **Active combat — highest SSH2 data-read volume (220K)** | **trace_pd_combat.bin** |

### Provenance

| Artifact | Value |
|----------|-------|
| Saturnis commit | **TODO: run `git rev-parse HEAD` in Saturnis repo and pin here** |
| Ymir trace fork commit | **TODO: run `git rev-parse HEAD` in Ymir fork repo and pin here** |
| Trace binary format | 48-byte records, 8-byte header (`BusTraceFileHeader`) |
| Record struct | `BusTraceBinaryRecord` in bus_trace.cpp |
| Summary schema version | v3 (will bump to v4 with Phase 1/2 changes) |
| Replay tool | `trace_replay.exe` (Saturnis, tools/trace_replay/) |
| Game tested | Panzer Dragoon Saga (AZEL_1.chd, disc 1) |
| BIOS | Auto-detected IPL ROM via Ymir |

**Rule:** Provenance pins must be exact commit SHAs, not branch names or `HEAD`. Run `git rev-parse HEAD` at the time of each report or summary freeze. Update pins whenever commits are pushed or trace format changes. If a pin says `TODO`, the associated report is not considered reproducible.

---

## Ymir Fork Changes

| Change | File | Purpose |
|--------|------|---------|
| Auto-start capture | bus_trace.cpp | `YMIR_BUS_TRACE_AUTO_START=1` for boot traces |
| ymir_timing.cpp fixes | ymir_timing.cpp | Address range corrections |
| busarb.cpp fixes | busarb.cpp | Arbiter configuration corrections |

Fork remains observation-only. No timing model changes to Ymir.

---

## Research Hygiene Rules

### Core Rules (Unchanged)
- Do not interpret deltas without dataset hygiene summary visible
- Keep outputs deterministic and diff-friendly
- Never modify Ymir timing model in the fork

### New Rules (Post-Retraction)

1. **Trace-first:** All analysis starts with trace-observed fields. Model comparisons come second and are labeled.

2. **Source labels are mandatory:** Every metric must declare `TRACE_ONLY`, `MODEL_COMPARISON`, or `HYBRID`. No exceptions.

3. **Raw data spot-check before conclusions:** Before any finding is written up, dump at least 20 raw records matching the finding's bucket using PowerShell/hex dump and verify the claim holds at the record level. The P0-A retraction was triggered by a 30-second raw byte dump — that should have happened first, not last.

4. **Cross-tabs before conclusions:** Always show master × region × access_kind × cache_bucket before attributing patterns to any cause.

5. **Sample size always visible:** Every bucket shows N. Buckets with N < 100 are flagged `LOW_SAMPLE` and excluded from conclusions.

6. **Symmetry checks before asymmetry claims:** Before claiming "CPU X behaves differently from CPU Y," show the distributions side by side within the same cache/region/kind bucket. The P0-A false positive would have been caught immediately by comparing MSH2 and SSH2 `elapsed` distributions directly.

### Conclusion Gate Checklist

Before any finding is reported upstream (to StrikerX3 or documented as "confirmed"):

- [ ] Finding is supported by trace-observed fields, not only model deltas
- [ ] Raw record spot-check performed (≥20 records matching the bucket)
- [ ] Cache bucket shown (for any analysis involving cached memory regions)
- [ ] Cross-tab shown (master × region × access_kind at minimum)
- [ ] Sample size shown and adequate (N ≥ 100 per bucket)
- [ ] Confounders listed (cache state, access size, region, workload phase)
- [ ] Symmetry check performed (MSH2 vs SSH2 within same bucket)
- [ ] Model-based statements explicitly labeled as hypothesis
- [ ] Finding reproducible by examining raw trace fields without running the arbiter

---

## Document History

| Version | Date | Status | Key Changes |
|---------|------|--------|-------------|
| v1 | 2026-02-24 | Superseded | Original TODO post-P0-A. Claimed SSH2 +1 as confirmed finding. |
| v2 (revised) | 2026-02-24 | Superseded | Added Codex feedback, binary reader priority, MA/IF breakdown. Still carried incorrect P0-A conclusion. |
| **v3** | **2026-02-24** | **Current** | **P0-A retracted. Full recovery plan. Trace-fact vs model-comparison split. Cache bucketing. New hygiene rules. Conclusion gate checklist.** |
