# Saturnis Agent Instructions

## Current Project Scope (Read First)

Saturnis is currently a **testing/scaffold host** for a Ymir-focused timing tool project.

### Active project goals
The active deliverables are:

- `libbusarb` (standalone arbitration seam/library)
- offline trace replay + diff tooling
- Ymir-calibrated timing callbacks
- deterministic tests and reproducible outputs
- handoff/integration documentation for Ymir

### What Saturnis is used for right now
Saturnis code may still be used for:

- arbiter experiments
- deterministic test harnesses
- prototype validation scaffolding
- internal comparison/reference behavior

### Not the current goal
Saturnis is **not** currently being developed as a standalone Saturn emulator product goal.

That means emulator-core expansion (decode coverage, interpreter refactors, BIOS boot progress, subsystem bring-up) is **deferred** unless it directly supports the Ymir timing tool workflow.

---

## Scope Freeze Rules (Active Until Changed)

### Do prioritize
- offline-first tooling (`trace_replay`, schemas, diff/annotated output)
- `libbusarb` extraction/hardening
- deterministic replay semantics
- Ymir timing callback parity and replay calibration
- tests that improve confidence in arbitration/replay correctness
- documentation that reduces handoff friction for Ymir integration

### Do not prioritize
- SH-2 decode table expansion for standalone emulator goals
- opcode enum/dispatch refactors
- `sh2_core.cpp` execution refactors not required for tool validation
- BIOS boot-path progress
- VDP/SCSP/SCU subsystem expansion unrelated to timing-tool validation
- “feature breadth” emulator work that does not improve the Ymir tool deliverables

### If unsure whether work is in scope
Ask:
> “Does this directly improve the offline Ymir timing tool, replay pipeline, arbiter parity, or handoff docs?”

If **no**, defer it and document it in a TODO / deferred list instead of implementing it.

---

## Build / Test Discipline (Always)

Always run the full build and test loop after changes (unless the change is docs-only and you explicitly note that no build/test was needed):

- `cmake -S . -B build`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

If the repo has multiple target groups or long-running suites, at minimum:
1. run all affected tests, and
2. run the full suite before finalizing changes when practical.

When you skip anything, say exactly what was skipped and why.

---

## Determinism Rules (Non-Negotiable)

Preserve determinism in all arbiter/replay logic.

- no wall-clock timing in emulation/tool logic
- no random behavior unless using a fixed, documented seed
- traces must be stable between identical runs
- replay outputs (including annotated output) must be reproducible for the same input/config
- ordering must be explicit and deterministic (never rely on container iteration order if unspecified)

### Replay determinism specifics
- `seq` is the authoritative deterministic tie-break for equal-time records
- replay ordering must be documented and consistent
- comparative replay must not mutate downstream record timing based on earlier mismatches

---

## Phase 1 Trace / Replay Semantics (Important)

These rules prevent semantic drift in the Ymir timing tool workflow.

### Trace format (Phase 1)
Phase 1 trace schema uses explicit timing fields:

- `service_cycles`
- `retries`
- ticks (e.g. `tick_first_attempt`, `tick_complete`)
- `seq` (monotonic ordering)

### Do not use ambiguous `stall` field
- Do **not** add or rely on a raw `stall` field in Phase 1 trace schema.
- If a comparison needs “stall-like” values, derive them in tooling and label them clearly.

### Comparative replay only
- Replay is **comparative**, not retiming.
- Do **not** rewrite downstream record timing because the arbiter disagrees with an earlier record.
- Use recorded trace timing as the fixed input; compare arbiter predictions against it.

### Known-gap classification
- Classify known Ymir behavior gaps separately from arbiter mismatches.
- Example: byte-size access wait-path differences (if present in current Ymir behavior) should be tagged as **known-gap**, not treated as arbiter failures.

### Annotated output naming
Use disambiguated field names in annotated/diff output (examples):
- `ymir_service_cycles`
- `ymir_retries`
- `ymir_effective_wait`
- `ymir_effective_total`
- `arbiter_wait`
- `arbiter_total`
- `classification`
- `known_gap_reason` (when applicable)

Do not overload ambiguous names in comparison output.

---

## Public API / Dependency Boundaries (`libbusarb`)

Keep `libbusarb` clean and portable.

### Public seam requirements
- public headers under `include/busarb/` must remain Saturnis-independent
- no Saturnis internal types in public interfaces
- prefer POD-style request/result/config types
- query/commit semantics should remain explicit and deterministic

### Hot-path constraints (arbiter code)
Avoid adding heavy constructs to arbiter hot logic:
- no heap allocations
- no string-heavy logic
- no maps/unordered_maps
- no virtual dispatch unless already required and justified

### Offline tooling exception
Offline tools (e.g. trace replay) may use normal convenience libraries:
- JSON parsing
- strings
- file IO
- richer diagnostics

…but keep those dependencies out of the core arbiter path.

---

## Correctness > Feature Breadth

Prefer correctness, explicit semantics, and testability over adding more functionality.

When behavior is unknown or incomplete:
- add a focused `TODO`
- add a targeted test when possible
- document assumptions clearly (especially if derived from Ymir behavior)

Do not “fill gaps” with guesswork when the semantics matter for comparison tooling.

---

## Testing Expectations by Change Type

### Arbiter logic changes
Must include:
- deterministic behavior tests
- tie-break behavior tests
- regression test for the changed case
- confirmation that existing arbiter tests still pass

### Replay / trace tooling changes
Must include:
- parser/schema tests or fixture coverage
- malformed line handling behavior
- deterministic ordering behavior (equal-time tie via `seq`)
- classification coverage (agreement / mismatch / known-gap where applicable)

### Docs-only changes
- build/test may be skipped, but state this explicitly in the summary

---

## Documentation Rules

Keep docs aligned with the active project focus.

### Active docs should be easy to find
Prioritize clarity for:
- active roadmap
- trace format spec
- Ymir integration guide
- replay/diff semantics
- known-gap classifications / limitations

### Archive instead of deleting context
Do not delete useful prior roadmap/thinking unless clearly redundant and safe.
Prefer:
- marking old docs as archived/superseded
- moving them under `docs/archive/`
- linking from active docs when helpful for historical context

### Be precise about claims
Do not claim:
- “hardware-correct”
- “Saturn-accurate”
- or equivalent broad guarantees

…unless the evidence actually supports it.

For Phase 1, the correct framing is closer to:
- deterministic offline comparison/calibration tooling
- Ymir behavior comparison/reference
- known limitations documented

---

## External Dependencies / Runtime Constraints

- Do not introduce external downloads or runtime network dependencies.
- Do not include BIOS/ROM assets; only load user-provided files.
- Keep offline tooling self-contained within repo dependencies whenever possible.
- Prefer existing project dependencies over adding new ones.

---

## Communication / Output Expectations (for agent-generated changes)

When proposing or submitting changes, include:

1. **What changed**
2. **Why it changed** (tie back to active Ymir-tool scope)
3. **Files modified**
4. **Tests run** (exact commands + results)
5. **Any assumptions / known limitations**
6. **Deferred follow-ups** (if applicable)

If a requested task is out of current scope, say so and suggest the smallest scoped version that supports the active goals.

---

## Quick Scope Check Before You Start (Checklist)

Before implementing, verify:

- [ ] Does this directly support `libbusarb`, trace replay, timing callbacks, tests, or Ymir handoff docs?
- [ ] Does this preserve determinism?
- [ ] Are trace/replay semantics still explicit (`service_cycles`, `retries`, ticks, `seq`)?
- [ ] Am I avoiding emulator-core scope creep?
- [ ] Do I know what tests I need to run after the change?

If multiple answers are “no,” stop and rescope before coding.
