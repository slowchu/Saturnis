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

13. [x] Add deterministic mixed-size contention tests where both CPUs issue source-clear operations with overlapping masks in the same batch.
14. [x] Add BIOS trace count assertions for `BARRIER` plus a deterministic zero-count guard for future DMA-tagged commits until DMA paths are modeled.
15. [x] Expand SH-2 delay-slot matrix to include MOV.W/MOV.L store delay-slot combinations under BRA/RTS.
16. [x] Add deterministic commit-horizon tests that cycle progress updates between CPUs while pending queues carry mixed RAM/MMIO operations.

## Challenge session tasks (expanded to 16 and completed)

1. [x] Add deterministic SCU same-batch overlapping source-set/source-clear contention coverage.
2. [x] Add deterministic repeated-run coverage for overlapping mixed-size SCU source set/clear contention.
3. [x] Add deterministic SCU overlap coverage that validates masked IST visibility through IMS.
4. [x] Add deterministic BIOS fixture count-stability checks for `cache_hit:true` commits.
5. [x] Add deterministic BIOS fixture count-stability checks for `cache_hit:false` commits.
6. [x] Keep BIOS `BARRIER` commit-count stability assertions active across repeated runs.
7. [x] Keep BIOS DMA-tagged commit-count checks pinned to deterministic zero until DMA modeling exists.
8. [x] Add deterministic dual-demo multithread repeated-run checks for stable `BARRIER` commit counts.
9. [x] Expand SH-2 delay-slot matrix with BRA + MOV.W store cases where delay-slot and target both write the same address.
10. [x] Expand SH-2 delay-slot matrix with RTS + MOV.W store cases where delay-slot and target both write the same address.
11. [x] Expand SH-2 delay-slot matrix with BRA + MOV.L store cases where delay-slot and target both write the same address.
12. [x] Expand SH-2 delay-slot matrix with RTS + MOV.L store cases where delay-slot and target both write the same address.
13. [x] Add deterministic commit-horizon coverage for mixed RAM/MMIO queues that drain across three horizon-advance cycles.
14. [x] Add deterministic commit-horizon coverage asserting pending-order preservation across long queue drain cycles.
15. [x] Refresh architecture notes to describe the expanded overlap, count-stability, and delay-slot coverage.
16. [x] Refresh code-review snapshot to document the completed 16-item challenge and residual risks.

## Next tasks

1. [ ] Add deterministic SCU source-mask contention tests that interleave overlapping source-set and source-clear writes across two consecutive batches with round-robin winner rotation checks.
2. [ ] Add deterministic BIOS trace assertions comparing per-kind commit counts between single-thread and multithread dual-demo runs (`IFETCH/READ/WRITE/MMIO/BARRIER`).
3. [ ] Expand SH-2 delay-slot matrix with mixed-width target-side overwrite checks (delay-slot MOV.W store then target MOV.L store to same address, and inverse).
4. [ ] Add deterministic commit-horizon regression coverage for queues requiring four or more horizon-advance cycles with interleaved MMIO reads/writes and RAM writes.

## Notes

- Keep all device semantics deterministic and test-backed.
- Prefer focused register-level behavior over broad partially-modeled subsystems.
