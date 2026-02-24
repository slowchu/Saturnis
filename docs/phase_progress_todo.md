# Saturnis → Ymir Progress TODO

This checklist tracks implementation status against the current roadmap direction.

## Phase 1 — Offline-first replay pipeline

### 1.1 Trace format spec
- [x] Add `docs/trace_format.md` with per-successful-access schema.
- [x] Document `service_cycles` + `retries` semantics (no raw `stall`).
- [x] Document known byte-access Ymir wait-model gap classification.
- [x] Document `seq` ordering semantics and non-blocking diagnostics.

### 1.2 Ymir-calibrated access cycles callback
- [x] Add public callback declaration (`include/busarb/ymir_timing.hpp`).
- [x] Implement region timing table (`src/busarb/ymir_timing.cpp`).
- [x] Add boundary + unmapped fallback tests (`tests/test_ymir_timing.cpp`).

### 1.3 Comparative trace replay tool
- [x] Add `tools/trace_replay/trace_replay.cpp` target.
- [x] Parse Phase 1 JSONL schema and skip malformed lines with warnings.
- [x] Replay using stable `(tick_complete, seq)` ordering.
- [x] Compare Ymir-derived timing vs arbiter timing (comparative-only; no retiming).
- [x] Classify agreement / mismatch / known-gap.
- [x] Emit annotated output with disambiguated fields (`--annotated-output`).
- [x] Emit machine-readable summary (`--summary-output`).
- [x] Emit known-gap summary counters including byte-access count.
- [x] Emit seq-quality diagnostics (`duplicate_seq_count`, `non_monotonic_seq_count`).
- [x] Add replay fixture + automated test coverage.

### 1.4 Public busarb parity: same-address contention + tie turnaround
- [x] Add configurable penalties via `ArbiterConfig`.
- [x] Apply same-address contention penalty in commit path.
- [x] Apply tie-turnaround penalty in commit path via explicit `had_tie` argument.
- [x] Add/extend tests for these behaviors.

### 1.5 Public busarb parity: CPU round-robin tie-break
- [x] Keep DMA top priority.
- [x] Alternate SH2_A/SH2_B winners on equal-priority ties.
- [x] Add/extend tests for round-robin behavior.

### 1.6 Ymir integration guide
- [x] Add `docs/ymir_integration_guide.md` with Phase 1 scope.
- [x] Document optional trace emission points and state persistence requirement.
- [x] Document replay usage and delta interpretation.
- [x] Document fixed Phase 1 assumptions (per-successful-access, byte-gap non-blocking).

## Phase 1 hardening / follow-up tasks

- [x] Add API version markers in public busarb header.
- [ ] Replay at least one real Ymir-produced trace and archive output artifacts.
- [ ] Add golden-file comparison for annotated replay output format stability.
- [ ] Expand known-gap taxonomy beyond byte-access gap (if needed from real traces).
- [ ] Add optional timing-table override input for faster calibration iteration.

## Phase 2+ (deferred)

- [ ] Calibration runner script (automated replay + regional delta analysis).
- [ ] Runtime Ymir integration overhead validation (separate from Phase 1).
- [ ] IF/MA stage-aware contention modeling.
- [ ] Closed-loop retiming propagation.
