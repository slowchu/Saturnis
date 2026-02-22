# Saturnis — Ymir Offline Timing Tool Host

Saturnis is now focused on one deliverable: a **deterministic, offline bus-timing reference/replay toolchain for Ymir**.

This repository is no longer pursuing “full standalone Sega Saturn emulator” as its active product direction.

## Current Scope (active)

Saturnis currently exists to support Ymir timing validation through:

- `libbusarb` as a Saturnis-independent arbitration seam/library
- offline-first trace capture/replay and comparative replay diffing
- Ymir-calibrated timing callbacks and known-gap classification
- deterministic replay ordering and reproducible tests
- lightweight harness/scaffold code used to generate and validate traces

Start here:

- `docs/roadmap_ymir_tool.md`
- `docs/trace_format.md`
- `docs/ymir_integration_guide.md`
- `docs/project_scope_rules.md`

## Active vs Frozen

### Active

- `libbusarb` correctness and stable interfaces
- replay/diff tooling (`trace_replay`) and trace documentation
- deterministic test coverage for arbitration/replay semantics
- Ymir integration guidance and calibration workflow

### Frozen / De-emphasized

- broad emulator-core expansion as a standalone end goal
- large decode/dispatch growth not required for replay validation
- full BIOS boot-path completion as a roadmap objective
- general “run commercial games” scope

Legacy emulator scaffolding is retained as internal validation infrastructure where it helps deterministic bus/timing testing.

## Why the pivot?

The project narrowed scope to ship a concrete, high-value deliverable faster:

- Ymir expressed interest in a deterministic offline timing reference to calibrate behavior safely.
- Offline-first comparative replay makes regressions measurable and reproducible.
- A smaller scope reduces contributor ambiguity and avoids broad emulator-roadmap churn.
- Existing Saturnis components remain useful as test harness/sandbox infrastructure.

## Current Deliverables

- `libbusarb` extraction/seam with Saturnis-independent public headers
- documented trace format and known-gap classification rules
- `trace_replay` tool for comparative replay and diff analysis
- Ymir integration guide (Phase 1 offline workflow)
- deterministic build/test workflow with stable regression fixtures

## Deferred / Parked Work

The following are intentionally parked unless directly needed for Ymir tool validation:

- decode expansion and interpreter breadth work
- SH-2 execution refactors aimed at emulator completeness
- full emulator BIOS/game boot path milestones
- graphics/audio subsystem fidelity expansion

Archived historical roadmap material is preserved in `docs/archive/`.

## Build and test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Active tools/targets

- `busarb` (library)
- `trace_replay` (offline comparative replay tool)
- `busarb_tests`, `ymir_timing_tests`, `saturnis_trace_regression` (deterministic validation)

## Scope note

`saturnemu` and emulator-core code are retained as scaffolding/sandbox code for deterministic timing validation. They are not the primary product deliverable in the current roadmap.

## Legal

- No BIOS/ROM content is included.
- You must supply your own Saturn BIOS file if you exercise BIOS-related harness paths.
