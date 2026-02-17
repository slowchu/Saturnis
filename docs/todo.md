# Saturnis TODO

## Completed backlog

1. [x] Add deterministic VDP2 TVSTAT MMIO semantics (`0x05F80004`) and regression coverage.
2. [x] Expand SCU interrupt register behavior beyond IMS masking (status/pending interactions).
3. [x] Continue SH-2 data-memory path expansion (additional memory op forms + focused tests).
4. [x] Grow BIOS bring-up coverage with deterministic trace assertions.

## Completed implementation queues

1. [x] Add deterministic SH-2 delayed-branch behavior coverage (BRA/RTS delay-slot semantics + regression tests).
2. [x] Add focused MMIO interrupt-source wiring into SCU pending bits (deterministic synthetic source model).
3. [x] Expand BIOS bring-up trace assertions to cover stable state snapshots (PC/register checkpoints).
4. [x] Add deterministic trace fixture comparison for a fixed BIOS mini-program across single/multirun execution.
5. [x] Add deterministic MMIO side-effect tracing assertions for SCU synthetic source register transitions.

## Completed in recent extended sessions

1. [x] Add deterministic BIOS trace assertions for slave CPU checkpoints (PC/register progression).
2. [x] Add focused tests for subword writes to SCU synthetic source set/clear registers.
3. [x] Add deterministic coverage for branch-in-delay-slot behavior policy (and document expected semantics).
4. [x] Add trace-level assertions that SCU synthetic-source MMIO writes appear in deterministic order in emitted JSONL commits.
5. [x] Add deterministic tests for mixed CPU contention when both CPUs perform SCU synthetic-source MMIO writes in the same arbitration window.
6. [x] Add explicit regression checks that MMIO commit `stall` fields for SCU synthetic-source writes remain stable under repeated runs.
7. [x] Expand BIOS checkpoint assertions to verify selected commit-event fields (`t_start`, `t_end`, `stall`) for a fixed instruction slice.
8. [x] Add deterministic coverage for RTS/BRA interactions with memory-op delay-slot instructions (e.g., MOV.W in delay slot).
9. [x] Add deterministic coverage for mixed-size SCU synthetic-source contention (byte/halfword/word interleavings across CPUs).
10. [x] Add regression checks that `MMIO_READ`/`MMIO_WRITE` trace commit counts remain stable for the BIOS fixture under repeated runs.
11. [x] Add focused branch-flow tests for combined branch-in-delay-slot plus memory-op delay-slot interactions.
12. [x] Add deterministic trace assertions for commit ordering under commit-horizon gating when only one CPU has pending MMIO.

## Next tasks

1. [ ] Add deterministic tests for mixed-size SCU contention that also includes concurrent source-clear operations from both CPUs.
2. [ ] Add BIOS trace assertions for stable counts of `IFETCH`/`READ`/`WRITE` commits (in addition to MMIO counts).
3. [ ] Expand SH-2 delay-slot matrix tests to cover MOV.L memory-op slots with BRA/RTS.
4. [ ] Add deterministic commit-horizon regression tests that interleave pending MMIO and RAM ops across both CPUs.

## Notes

- Keep all device semantics deterministic and test-backed.
- Prefer focused register-level behavior over broad partially-modeled subsystems.
