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

## Expanded next-task batch (16/16 completed)

1. [x] Add deterministic SCU overlap set/clear regression covering two same-batch operations.
2. [x] Add deterministic SCU overlap regression validating round-robin winner rotation across two consecutive batches.
3. [x] Add deterministic repeated-run SCU overlap regression for mixed-size set/clear sequences.
4. [x] Add deterministic commit-horizon regression for mixed RAM/MMIO queues draining across four progress cycles.
5. [x] Add deterministic commit-horizon regression validating queue-order preservation at each intermediate drain cycle.
6. [x] Add deterministic SH-2 BRA path regression for delay-slot MOV.W store followed by target MOV.L overwrite to same address.
7. [x] Add deterministic SH-2 BRA path regression for delay-slot MOV.L store followed by target MOV.W overwrite to same address.
8. [x] Add deterministic SH-2 RTS path regression for delay-slot MOV.W store followed by target MOV.L overwrite to same address.
9. [x] Add deterministic SH-2 RTS path regression for delay-slot MOV.L store followed by target MOV.W overwrite to same address.
10. [x] Add deterministic dual-demo trace assertion for `IFETCH` count parity between single-thread and multithread runs.
11. [x] Add deterministic dual-demo trace assertion for `READ` count parity between single-thread and multithread runs.
12. [x] Add deterministic dual-demo trace assertion for `WRITE` count parity between single-thread and multithread runs.
13. [x] Add deterministic dual-demo trace assertion for `MMIO_READ` count parity between single-thread and multithread runs.
14. [x] Add deterministic dual-demo trace assertion for `MMIO_WRITE` count parity between single-thread and multithread runs.
15. [x] Add deterministic dual-demo trace assertion for `BARRIER` count parity between single-thread and multithread runs.
16. [x] Refresh docs (`todo`/`code_review`/`architecture`) to record completed coverage and updated follow-ups.

## Next tasks

1. [ ] Add deterministic SCU overlap contention tests where both CPUs issue source-set and source-clear writes on different byte lanes within the same two-batch window.
2. [ ] Add deterministic SCU overlap contention tests with staggered `req_time` values that still resolve identically under commit-horizon gating.
3. [ ] Add deterministic SCU overlap tests that assert IST clear (`0x05FE00A8`) behavior when overlap bits are masked and then unmasked.
4. [ ] Add deterministic SCU write-log assertions for overlap set/clear scenarios to validate address/value/size ordering metadata.
5. [ ] Add deterministic dual-demo per-kind count assertions for `src` field parity (`READ`/`MMIO`/`BARRIER`) between single-thread and multithread traces.
6. [ ] Add deterministic dual-demo repeated-run assertions for `cache_hit:true` and `cache_hit:false` counts.
7. [ ] Add deterministic BIOS fixture assertions for per-CPU commit-kind distributions (cpu0 vs cpu1) remaining stable across runs.
8. [ ] Add deterministic BIOS fixture assertions for `t_start/t_end/stall` tuples of selected MMIO commits across repeated runs.
9. [ ] Expand SH-2 delay-slot matrix with BRA path mixed-width overwrite tests where target writes happen via MOV.W after MOV.L and include negative immediates.
10. [ ] Expand SH-2 delay-slot matrix with RTS path mixed-width overwrite tests where target writes happen via MOV.L after MOV.W and include negative immediates.
11. [ ] Add deterministic SH-2 same-address overwrite tests where delay-slot writes MMIO and target writes RAM (and inverse where supported).
12. [ ] Add deterministic commit-horizon tests for five-cycle drains with interleaved MMIO reads, MMIO writes, and RAM writes.
13. [ ] Add deterministic commit-horizon tests that verify response values from queued MMIO reads when preceding queued writes are horizon-blocked.
14. [ ] Add deterministic commit-horizon tests that alternate CPU progress updates asymmetrically before final convergence.
15. [ ] Add deterministic trace-order assertions that mixed commit kinds (`READ/WRITE/MMIO/BARRIER`) remain in stable serialized order under repeated runs.
16. [ ] Run code-review pass and document newly discovered risks/TODOs after implementing the above batch.

## Notes

- Keep all device semantics deterministic and test-backed.
- Prefer focused register-level behavior over broad partially-modeled subsystems.
