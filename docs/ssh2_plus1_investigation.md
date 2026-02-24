# SSH2 +1 Timing Delta Investigation (P0-A/B/C, P1 triage)

## Scope
This document records completion of the updated investigation plan:
- P0-C: always-on dataset hygiene summary in replay output
- P0-B: timing decomposition fields (`base_latency`, `contention_stall`, `total_predicted`)
- P0-A: hypothesis check using available traces in this repository/session
- P1: conditional BSC capture decision

## P0-C completion
`trace_replay` now prints dataset hygiene before delta analysis and emits the same composition in JSON summary:
- `total_events`, `included_events`, `excluded_events`
- exclusion reasons (`malformed_line`, `invalid_master`)
- known-gap candidate bucket counts
- included distributions by master, region, size (`B/W/L`), and `R/W`

The output order is fixed and deterministic.

## P0-B completion
Per-record annotated output now includes decomposition aliases:
- `base_latency`
- `contention_stall`
- `total_predicted`

Summary JSON includes aggregate means:
- `mean_base_latency`
- `mean_contention_stall`
- `mean_total_predicted`

Schema version is bumped to `3`.

## P0-A experiment and evidence
### Dataset A: provided 200K Drive trace
Command:
- `./build/trace_replay /tmp/ymir_drive_trace.jsonl --summary-output docs/replay_outputs/drive_trace_summary_v6.json --summary-only --top-k 20`

Composition from hygiene summary:
- regions: High WRAM + SH-2 on-chip regs only
- no BIOS/WRAM-L/other external regions in this sample

Result:
- normalized deltas are no longer a constant `-1`
- `normalized_agreement_count = 158302`
- `normalized_mismatch_count = 41698`

### Dataset B: repository sample fixture (small, mixed regions)
Command:
- `./build/trace_replay tests/fixtures/trace_replay/sample_trace.jsonl --summary-output /tmp/sample_summary_new.json --annotated-output /tmp/sample_annotated_new.jsonl --top-k 5`

Composition includes non-High-WRAM regions (SMPC/VDP/SCU/Low WRAM).

Result:
- normalized deltas vary (not a uniform +1/-1 pattern)
- known byte-path caveat remains separately classified as known gap

## Conclusion (with current data)
The available large trace does **not** provide region diversity needed for a decisive global-vs-WRAM-H determination, because it is dominated by High WRAM and on-chip accesses.

Current evidence supports:
- the previous systematic off-by-one artifact is resolved
- decomposition/hygiene now make future hypothesis tests interpretable

Current status of the root-cause question is therefore:
- **inconclusive pending a diversified SSH2 trace** (WRAM-H + non-WRAM-H in the same trusted-class analysis window)

## P1 decision
P1 (BSC register capture) remains **conditional**.
Given the current inconclusive region diversity, the next highest-value step is first to collect a diversified SSH2 dataset (boot-oriented capture if needed). If that run shows WRAM-H-only +1 behavior, prioritize BSC instrumentation (`WCR/BCR1/MCR`) immediately.
