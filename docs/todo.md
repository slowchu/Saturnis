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

## Current 16-task batch (completed)

1. [x] Add deterministic SCU overlap contention coverage with different byte-lane set/clear writes across a two-batch window.
2. [x] Add deterministic SCU overlap contention coverage with staggered `req_time` values under commit-horizon gating.
3. [x] Add deterministic SCU overlap coverage for IST clear (`0x05FE00A8`) behavior while bits are masked, then unmasked.
4. [x] Add deterministic SCU overlap write-log assertions for address/value ordering metadata.
5. [x] Add deterministic dual-demo `src`-field count parity assertions (`READ`/`MMIO`/`BARRIER`) between single-thread and multithread traces.
6. [x] Add deterministic dual-demo repeated-run assertions for `cache_hit:true` and `cache_hit:false` counts.
7. [x] Add deterministic BIOS fixture assertions for per-CPU commit-kind distribution stability across runs.
8. [x] Add deterministic BIOS fixture assertions that selected MMIO commit timing-tuple lines remain stable across runs.
9. [x] Expand SH-2 delay-slot matrix with BRA mixed-width overwrite checks using negative immediates.
10. [x] Expand SH-2 delay-slot matrix with RTS mixed-width overwrite checks using negative immediates.
11. [x] Add focused TODO+determinism guard for MMIO-vs-RAM same-address overwrite flow (not representable in current SH-2 subset).
12. [x] Add deterministic commit-horizon coverage for five-cycle drains with interleaved MMIO reads/writes and RAM writes.
13. [x] Add deterministic commit-horizon coverage verifying queued MMIO read responses while preceding writes are horizon-blocked.
14. [x] Add deterministic commit-horizon coverage with asymmetric progress updates before final convergence.
15. [x] Add deterministic mixed-kind trace-order assertions across repeated multithread dual-demo runs.
16. [x] Run code-review pass and document updated risks/TODOs after implementing this batch.

## Next 16-task batch (completed)

1. [x] Add deterministic SCU overlap tests that combine byte/halfword writes in the same batch and assert lane-accurate IST visibility.
2. [x] Add deterministic SCU overlap tests that validate source-clear idempotence when identical clear masks are replayed across runs.
3. [x] Add deterministic SCU overlap tests verifying IST/source register views remain consistent after alternating set/clear bursts.
4. [x] Add deterministic SCU overlap write-log regression that checks monotonic timestamp deltas across repeated runs.
5. [x] Add deterministic dual-demo `src`-field parity checks per CPU (`cpu0` vs `cpu1`) across single-thread and multithread runs.
6. [x] Add deterministic dual-demo commit timing-window checks (`t_start/t_end/stall`) for selected mixed-kind commits.
7. [x] Add deterministic BIOS fixture assertions for per-kind `src` distributions across repeated runs.
8. [x] Add deterministic BIOS fixture assertions that selected MMIO commit ordering relative to IFETCH remains stable.
9. [x] Expand SH-2 delay-slot matrix with BRA mixed-width overwrite cases where both source immediates are negative.
10. [x] Expand SH-2 delay-slot matrix with RTS mixed-width overwrite cases where both source immediates are negative.
11. [x] Add deterministic SH-2 overwrite tests for same-address sequences that include an extra non-memory instruction between delay-slot and target stores.
12. [x] Add deterministic commit-horizon tests for six-cycle drains with alternating MMIO read/write pressure.
13. [x] Add deterministic commit-horizon tests that pin expected values for every queued MMIO read in a long pending sequence.
14. [x] Add deterministic commit-horizon tests that exercise horizon advances from asymmetric updates on both CPUs in alternating order.
15. [x] Add deterministic trace-order assertions over repeated runs for exact commit-line prefixes of mixed kinds.
16. [x] Run another code-review pass and refresh docs with newly identified risks and TODOs.

## Next tasks

1. [ ] Add deterministic SCU overlap tests combining halfword clear with byte set on opposite lanes while IMS masks one lane.
2. [ ] Add deterministic SCU overlap tests validating IST clear idempotence when replayed with interleaved source-set writes.
3. [ ] Add deterministic SCU overlap tests comparing source register and IST views under alternating mask/unmask windows.
4. [ ] Add deterministic SCU write-log checks for per-CPU entry counts under repeated overlap contention bursts.
5. [ ] Add deterministic dual-demo per-CPU `src:"MMIO"` parity checks between single-thread and multithread traces.
6. [ ] Add deterministic dual-demo selected commit-line timing tuple checks (`t_start/t_end/stall`) across repeated runs.
7. [ ] Add deterministic BIOS fixture checks for per-CPU `src` distributions across repeated runs.
8. [ ] Add deterministic BIOS fixture checks for first MMIO_READ ordering relative to IFETCH commits.
9. [ ] Expand SH-2 delay-slot matrix with BRA both-negative mixed-width overwrite plus follow-up target arithmetic checks.
10. [ ] Expand SH-2 delay-slot matrix with RTS both-negative mixed-width overwrite plus follow-up target arithmetic checks.
11. [ ] Add deterministic SH-2 overwrite test with two non-memory instructions between delay-slot and target store.
12. [ ] Add deterministic commit-horizon regression for seven-cycle drains with alternating RAM/MMIO pressure.
13. [ ] Add deterministic commit-horizon regression pinning values for four queued MMIO reads in one pending sequence.
14. [ ] Add deterministic commit-horizon regression where CPU progress alternation reverses midway before convergence.
15. [ ] Add deterministic trace-prefix assertions for the first 12 commit lines across repeated multithread runs.
16. [ ] Run another code-review pass and refresh docs with any newly discovered risks/TODOs.

## Notes

- Keep all device semantics deterministic and test-backed.
- Prefer focused register-level behavior over broad partially-modeled subsystems.
