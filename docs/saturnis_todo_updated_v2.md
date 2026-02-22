# Saturnis — Project Status Assessment & TODO Roadmap

*As of the February 22, 2026 codebase snapshot*

---

## Executive Summary

Saturnis has evolved from a broad "Sega Saturn emulator" ambition into a focused **offline-first Ymir timing tool** project. The pivot was smart — it trades a massive, years-long emulator effort for a concrete, shippable deliverable: a deterministic bus-timing reference and comparative replay toolchain for the Ymir Saturn emulator.

The project has four active deliverable tracks (A through D), and the current codebase shows substantial completed work on the scaffolding, testing, and internal SH-2/device infrastructure. However, most of that completed work lives in the *legacy scaffold* layer. The actual Ymir-facing deliverables — the parts someone outside this repo would consume — still have meaningful gaps.

Below is a blunt assessment of where things stand and a prioritized, step-by-step plan to get each track across the finish line.

---

## Current State by Track

### Track A — `libbusarb` Seam Hardening

**Status: ~70% complete — Core API works, but integration-readiness has gaps.**

What exists and is solid:
- Public headers under `include/busarb/` are clean and Saturnis-independent.
- `Arbiter` class with `query_wait()` (non-mutating) and `commit_grant()` (mutating) API.
- `pick_winner()` with deterministic fixed-priority tie-break (DMA > SH2_A > SH2_B) plus CPU round-robin.
- `ymir_timing.cpp` region-based timing lookup table (15 Saturn memory regions).
- Deterministic unit tests covering query order-independence, commit determinism, callback-driven service timing, and tie-break policy.
- `ArbiterConfig` for same-address contention and tie turnaround penalties.

What's missing or incomplete:
- **No versioned API contract.** There is no version number, ABI stability guarantee, or changelog. If Ymir links against this, any header change silently breaks them.
- **Timing table is placeholder-grade.** `ymir_access_cycles()` uses flat per-region cycle counts with no size-dependent variation (the `size_bytes` parameter is ignored). Real Saturn bus timing varies by access width.
- **No MA/IF stage-aware contention model.** Documented as deferred to future contention-modeling work, but this is a significant fidelity gap.
- **No integration smoke test against Ymir.** The tests validate internal correctness, but nobody has confirmed the adapter pattern (`query → pick_winner → commit`) actually works in a Ymir-like caller loop.
- **`pick_winner` has a dead variable.** `had_cpu_tie` is declared and never set to `true`, but `last_pick_had_cpu_tie_` is assigned from it. This suggests unfinished logic or a refactor artifact.
- **No error/diagnostic path.** If a caller passes invalid inputs (e.g., `size_bytes = 0`), behavior is unspecified.

### Track B — Trace Format & Comparative Replay

**Status: ~60% complete — Tool runs, but the workflow is fragile and under-documented.**

What exists and is solid:
- `trace_replay` CLI tool reads Phase 1 JSONL traces and produces annotated output with `--annotated-output` and `--top N`.
- Comparative replay logic: derives Ymir-side effective wait/total, computes arbiter predictions, classifies records as agreement / mismatch / known-gap.
- Known-gap classification for byte-access `IsBusWait()` omissions.
- Sample trace fixtures with CTest integration (10 records, validates `records_processed`, `malformed_lines_skipped`, `known_gap_count`).
- Hand-rolled JSON parser (no external dependency — good for portability).

What's missing or incomplete:
- **No real-world trace has ever been replayed.** All fixtures are synthetic. Until someone captures a real Ymir trace and runs it through `trace_replay`, there's no confidence the tool works on production data.
- **Delta histogram output is console-only.** No machine-readable summary output (e.g., JSON summary) for scripted calibration workflows.
- **Known-gap classification is hardcoded.** Only one gap type exists (byte-access wait path). There's no extensible classification registry or config-driven gap list.
- **No documentation of expected error modes.** What happens with out-of-order `seq`? Duplicate `seq`? Huge tick gaps? The tool silently skips malformed lines, but there's no guidance on what counts as "actionable" vs. "noise" in the output.
- **Trace format documentation may be stale.** The `trace_format.md` doc and the parser should be cross-checked to confirm they agree on every field.

### Track C — Ymir Calibration Workflow

**Status: ~40% complete — Guide exists, but the loop is not yet end-to-end runnable.**

What exists:
- `ymir_integration_guide.md` with Phase 1 scope, trace emission recommendations, replay instructions, and delta interpretation guidance.
- Adapter pattern documentation (query → pick_winner → commit).
- Region timing table as a starting calibration baseline.

What's missing or incomplete:
- **No runnable calibration script.** There is no `tools/calibrate.py` or equivalent that automates: capture Ymir trace → run replay → parse deltas → suggest timing adjustments. The guide describes the workflow conceptually but you'd have to do every step manually.
- **Timing table has no provenance or justification.** The cycle counts in `ymir_timing.cpp` are flat constants with no comments explaining where they came from (hardware docs, Ymir measurement, educated guess). This matters because the whole point of the tool is calibration.
- **No feedback-loop tooling.** After running a replay and seeing mismatches, there's no tooling that helps you update the timing table and re-run to measure improvement.
- **Integration guide assumes Ymir-side trace emission exists.** It doesn't (yet). Phase 1 is documented as "Ymir integration is deferred," but the guide still tells Ymir developers to add trace emission. This is a dependency that is completely outside this repo.

### Track D — Deterministic Validation

**Status: ~85% complete — This is the strongest part of the project.**

What exists and is solid:
- Extensive deterministic regression test suites (`test_kernel.cpp` at ~6000+ lines, `test_trace_regression.cpp` at ~800+ lines, `test_busarb.cpp` at ~400+ lines).
- Deterministic replay ordering enforced by `seq` tie-break.
- Commit-horizon progress watermarks for dual-CPU deterministic safety.
- Single-thread vs. multithread parity assertions.
- BIOS forward-progress instrumentation (local-only, with baseline diff tooling).
- CTest integration with all test targets.

What's missing or incomplete:
- **No CI pipeline.** The CTest suite exists, but there's no GitHub Actions / CI config that runs it automatically on push/PR. Determinism is only as good as the frequency of enforcement.
- **BIOS metrics workflow is entirely local.** This is by design (no BIOS assets in repo), but it means BIOS regression is only caught when a developer manually runs the local tooling.
- **Test naming conventions are documented but inconsistently followed.** `sh2_microtest_conventions.md` defines the naming pattern, but many tests in `test_kernel.cpp` predate it.

---

## Prioritized TODO List

The tasks below are ordered by impact on shipping the four tracks. Each task is tagged with the track it serves, an estimated size, and its dependencies.

### Phase 1: Ship-Blocking Essentials (get Track A + B to "usable by Ymir")

These are the minimum tasks to make `libbusarb` and `trace_replay` ready for someone outside this repo to actually use.

**1. Add API version constant to `libbusarb`**
- Track: A
- Size: Small (30 min)
- What: Add `constexpr int BUSARB_API_VERSION = 1;` to `busarb.hpp`. Document that breaking header changes increment this.
- Why: Without this, Ymir has no way to detect incompatible library versions at compile time.

**1b. Add trace/schema versioning for Phase 1 JSONL**
- Track: B
- Size: Small (1 hour)
- What: Add a documented trace format/schema version (field in records or file-level convention + parser constant) and define parser behavior on unknown/new fields (strict vs permissive).
- Why: The trace/replay contract will evolve just as quickly as the C++ API. Versioning prevents silent incompatibility during calibration and handoff.

**2. Fix the dead `had_cpu_tie` variable in `pick_winner()`**
- Track: A
- Size: Small (30 min)
- What: Either wire up the CPU tie detection logic (set `had_cpu_tie = true` when a round-robin tie actually occurs) or remove the dead variable and `last_pick_had_cpu_tie_` field. The current code silently never sets `had_tie = true` when calling `commit_grant` after a `pick_winner` that involved a CPU tie-break.
- Why: This is either a correctness bug (tie turnaround penalty is never applied in the `pick_winner` flow) or dead code that confuses readers.

**3a. Document current timing assumptions and provenance tags before changing values**
- Track: A, C
- Size: Small (1–2 hours)
- What: Add inline comments (or confidence tags) to current `ymir_timing.cpp` region constants: hardware-manual-derived / measured / estimated / placeholder. Note where `size_bytes` is currently ignored.
- Why: Prevents "fake precision" when improving the timing model and makes calibration diffs interpretable.

**3b. Add size-dependent timing to `ymir_access_cycles()` (provenance-aware)**
- Track: A, C
- Size: Medium (2–4 hours)
- What: The `size_bytes` parameter is currently ignored. Add width-dependent behavior only where justified (or clearly marked estimated), and cite the source/provenance for each changed region.
- Why: This is the core value proposition of the library. Flat per-region timing is too coarse for meaningful calibration.

**4. Add input validation to `libbusarb` public API (contract + diagnostics split)**
- Track: A
- Size: Small (1 hour)
- What: Define behavior for edge cases (`size_bytes = 0`, invalid widths, `now_tick` going backwards). Prefer debug assertions / documented caller contract for impossible inputs; optionally add a diagnostic mode for integration testing rather than forcing expensive runtime checks into the hot path.
- Why: Defensive API boundaries prevent silent corruption when integrated into a different codebase without needlessly bloating hot-path cost.

**5. Cross-validate `trace_format.md` against the actual parser**
- Track: B
- Size: Small (1–2 hours)
- What: Walk through every field in `trace_format.md` and confirm `trace_replay.cpp`'s `parse_record()` handles it. Flag any discrepancies. Update whichever side is wrong.
- Why: If the doc and parser disagree, Ymir developers will produce traces that the tool misparses.

**5b. Define parser strict/permissive behavior and document error modes**
- Track: B, D
- Size: Small-Medium (2–3 hours)
- What: Explicitly define and implement parser behavior for malformed JSONL lines, duplicate/out-of-order `seq`, missing fields, and unknown fields. Add strict mode (fail-fast) and permissive mode (skip + summarize) if practical.
- Why: This makes the tool usable both for automated calibration runs and for debugging messy early traces.

**5c. Add a table-driven mismatch/classification framework before first real trace**
- Track: B, D
- Size: Small-Medium (2–4 hours)
- What: Refactor/organize classification into a table-driven framework (or equivalent structured dispatch) with stable top-level buckets (`agreement`, `mismatch`, `known-gap`, `unsupported`, parser/schema errors as applicable). Seed only minimal known-gap rules now; populate/expand gap types after the first real trace.
- Why: Prevents the first real replay from becoming an unstructured flood of mismatches while still keeping known-gap taxonomy data-driven instead of speculative.

**6. Add a machine-readable JSON summary to `trace_replay`**
- Track: B, C
- Size: Medium (2–3 hours)
- What: Add `--summary-json <path>` output that writes `{ "records_processed": N, "agreement_count": N, "mismatch_count": N, "known_gap_count": N, "top_mismatches": [...] }`.
- Why: Enables scripted calibration loops. Console output is fine for humans but useless for automation.

**6b. Phase 1 exit gate: replay at least one real Ymir trace**
- Track: B, C
- Size: Medium (depends on capture availability; tool-side work is small once trace exists)
- What: Capture at least one real Ymir trace (even tiny / noisy / partial) and run it through `trace_replay`. Save annotated output + JSON summary and document what failed / what was classified as known-gap.
- Why: Until a real trace is replayed, confidence in the parser/replay workflow is limited to synthetic fixtures.

**7. Populate/expand known-gap classification rules after first real trace**
- Track: B
- Size: Small (1–2 hours)
- What: Use the first real Ymir trace to identify the highest-signal mismatch patterns, then add/expand known-gap classifiers (beyond the byte-access wait-path case) in the table-driven framework. Prioritize gap types that materially affect mismatch totals or top deltas (e.g., DMA timing, VDP coupling proxies, etc.).
- Why: Real traces should drive which gap classifications matter. Expanding taxonomy before seeing production-like data risks optimizing for synthetic cases.

**8. Write an end-to-end integration test with a realistic trace**
- Track: B, D
- Size: Medium (3–5 hours)
- What: Create a manually constructed but realistic ~50-record trace that exercises: multiple masters, contention, same-address overlap, DMA priority, known-gap records, malformed lines, and strict/permissive parser behavior. Run it through `trace_replay` and assert on annotated output + summary.
- Why: The current 10-record sample fixture is too small to catch real parsing/classification bugs.

### Phase 2: Calibration Workflow (make Track C actually functional)

**9. Complete timing table provenance coverage**
- Track: C
- Size: Small (1–2 hours)
- What: Finish provenance/confidence tagging for every entry in `kRegionTimings[]` and add a top-of-file note explaining the table's role in calibration (baseline vs measured vs estimated).
- Why: Without provenance, nobody knows which numbers are trustworthy and which need work.

**10. Add a timing table override mechanism**
- Track: A, C
- Size: Medium (2–4 hours)
- What: Allow `trace_replay` to accept an external timing table (JSON or similar) instead of only using the compiled-in `ymir_access_cycles`. This lets a calibration workflow iterate on timing values without recompiling.
- Why: Recompiling the library for every timing tweak is a friction multiplier that kills calibration velocity.

**11. Create a basic calibration runner script**
- Track: C
- Size: Medium-Large (4–8 hours)
- What: Write `tools/calibration/run_calibration.py` that automates: (1) take a Ymir trace JSONL, (2) run `trace_replay`, (3) parse the JSON summary, (4) print a per-region mismatch report, (5) suggest timing adjustments. Doesn't need to be fancy — even a simple "region X has N mismatches, average delta +2 cycles" report would be valuable.
- Why: This is the missing link between "tool exists" and "tool is useful for calibration."

**12. Stub out the Ymir-side trace emission patch guide**
- Track: C
- Size: Small (2–3 hours)
- What: Expand `ymir_integration_guide.md` §2 with a concrete, copy-pasteable code snippet showing where in a typical SH-2 memory handler to add trace emission. Include the persistent state slot for `tick_first_attempt`/`retries` across retries. Include compile-time gating via `#ifdef`.
- Why: Lowering the friction for the first Ymir trace capture is the single highest-leverage action for the entire calibration workflow.

**13. Add CI configuration**
- Track: D
- Size: Medium (2–4 hours)
- What: Add a `.github/workflows/ci.yml` (or equivalent) that runs `cmake + build + ctest` on push and PR. Include at least Linux (GCC + Clang). Optionally add ASan/UBSan builds.
- Why: Determinism guarantees mean nothing if the test suite only runs when a developer remembers to.

### Phase 3: Hardening & Sustainability

**14. Unify test naming to microtest conventions**
- Track: D
- Size: Large (ongoing, background task)
- What: Gradually rename tests in `test_kernel.cpp` to follow `sh2_microtest_conventions.md` patterns. Don't do this all at once — batch it with other changes to those test files.
- Why: Consistent naming makes it possible to grep for coverage gaps by cluster (alu/branch/mem/exception/decode/system).

**15. Add an API usage example / minimal integration harness**
- Track: A
- Size: Small (1–2 hours)
- What: Create `examples/minimal_adapter.cpp` showing the complete query → pick_winner → commit loop with a trivial timing callback. This is both documentation and a compile-time smoke test that the public API actually works from outside Saturnis.
- Why: The existing tests all link against internal Saturnis types. A standalone example proves the API is truly independent.

**16. Add `trace_replay` regression against annotated output**
- Track: B, D
- Size: Small (1–2 hours)
- What: The current CTest for `trace_replay` only checks console output via regex. Add a golden-file test that compares the annotated JSONL output line-by-line against a checked-in expected file.
- Why: Catch regressions in the annotated output format, not just the summary statistics.

**17. Assess and document the DMA path gap**
- Track: A, B
- Size: Medium (2–4 hours)
- What: The codebase has multiple TODOs about DMA trace/provenance. Write a focused gap-analysis note (in `docs/`) describing exactly what DMA scenarios the current arbiter handles, what it doesn't, and what trace records would expose the gap. Add a known-gap classification for DMA timing in `trace_replay`.
- Why: DMA is a major bus master on the Saturn. Pretending it doesn't exist limits the calibration tool's value on real workloads.

**18. Add BIOS metrics to CI (fixture-only)**
- Track: D
- Size: Small (already partially done)
- What: Confirm the Python-based `saturnis_bios_metrics_scripts` CTest target actually runs in CI (once CI exists). It uses fixtures, not real BIOS assets, so it's safe.
- Why: The parser/diff tests are already written. Just make sure they run automatically.

### Phase 4: Future Considerations (park these, but document them)

These are tasks that should NOT be started now, but should be acknowledged so they don't get lost:

- **IF/MA stage-aware contention model** — Needed for accurate SH-2 pipeline modeling. Major effort. Deferred to future contention-modeling work.
- **Closed-loop retiming propagation** — Where arbiter disagreements feed back into trace timeline. Out of scope for Phase 1 (comparative-only replay).
- **Runtime Ymir integration** — Running `libbusarb` in Ymir's hot path. Explicitly deferred.
- **Full VDP/SCSP temporal coupling** — Device-side timing effects on bus occupancy. Too complex for Phase 1 scope.
- **DMA priority starvation analysis** — Fixed-priority policy can starve lower-priority masters under sustained DMA load. Acknowledged in docs, needs future analysis if real traces show it.

---

## Suggested Execution Order

If you're working solo or in short sessions, here's the recommended sequence:

1. Tasks **1, 1b, 2, 3a, 4** (quick `libbusarb` + contract/schema hygiene + timing provenance docs)
2. Tasks **5, 5b, 5c, 6** (lock parser/doc semantics + classification scaffolding before bigger replay work)
3. Task **13** (add CI earlier so replay/parser regressions get caught automatically)
4. Task **6b** (replay the first real Ymir trace and capture annotated output + summary)
5. Tasks **7, 8** (use the first real trace to expand known-gap rules and harden realistic fixture coverage)
6. Tasks **3b, 9, 10, 11, 12** (improve timing fidelity and complete the calibration loop with override/provenance support)
7. Tasks **14–18** as time allows

## A Candid Note

The project has an impressive amount of internal infrastructure — the SH-2 interpreter coverage, device scaffolding, BIOS instrumentation, and test suite are all substantial engineering. But the *externally-facing deliverables* (what Ymir developers would actually touch) are still thin. The timing table is placeholder-grade, the replay tool has never seen a real trace, and there's no runnable calibration workflow.

The good news is that the hardest engineering work is already done. The remaining tasks are mostly about polish, documentation, and building the last-mile tooling that connects the internal engine to an external user. That's a much shorter path than where this project started.
