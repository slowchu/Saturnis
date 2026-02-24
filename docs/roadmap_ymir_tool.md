# Saturnis Active Roadmap — Ymir Offline Timing Tool

This is the **only active roadmap** for Saturnis.

Saturnis is an **offline-first** development/testing host for Ymir timing validation, centered on deterministic arbitration behavior and comparative replay.

## Goals

1. Ship a stable `libbusarb` seam usable independently from Saturnis internals.
2. Keep trace replay + diff workflows deterministic and easy to operate.
3. Improve Ymir timing calibration feedback through clear mismatch/known-gap classification.
4. Preserve deterministic replay ordering across runs, machines, and threading modes.

## Deliverable tracks

## Track A — `libbusarb` seam hardening

- Keep public interfaces Saturnis-independent.
- Document arbitration semantics and callback contracts.
- Add targeted tests whenever arbitration behavior changes.

Exit criteria:

- Stable header/API behavior covered by deterministic tests.
- No hidden dependency on emulator-only runtime state.

## Track B — Trace format and comparative replay

- Maintain trace schema docs and compatibility expectations.
- Keep `trace_replay` output focused on actionable parity deltas.
- Preserve known-gap classification for expected Ymir-vs-offline differences.

Exit criteria:

- Replay reports classify parity vs mismatch vs known-gap reliably.
- Trace docs match current parser/tool behavior.

## Track C — Ymir calibration workflow

- Maintain integration guide and trace capture expectations.
- Keep timing callback assumptions explicit and testable.
- Improve calibration loops without introducing runtime network dependencies.

Exit criteria:

- Integration guide supports repeatable offline calibration runs.
- Callback semantics remain deterministic and documented.

## Track D — Deterministic validation

- Prioritize deterministic replay ordering checks.
- Keep regression fixtures stable and reproducible.
- Treat nondeterminism as a blocker for merges in active tracks.

Exit criteria:

- Build + test loop remains stable on repeated runs.
- Ordering-sensitive regressions fail loudly and locally.

## Non-goals for this roadmap

- Expanding Saturnis into a full standalone emulator product.
- Decode/interpreter breadth work unrelated to Ymir validation.
- Full BIOS/game boot compatibility milestones.

## Relationship to legacy materials

Older broad emulator roadmaps are preserved under `docs/archive/` and are superseded by this document.
