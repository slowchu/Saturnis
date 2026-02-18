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

## Current 16-task batch (completed)

1. [x] Add deterministic SCU overlap tests combining halfword clear with byte set on opposite lanes while IMS masks one lane.
2. [x] Add deterministic SCU overlap tests validating IST clear idempotence when replayed with interleaved source-set writes.
3. [x] Add deterministic SCU overlap tests comparing source register and IST views under alternating mask/unmask windows.
4. [x] Add deterministic SCU write-log checks for per-CPU entry counts under repeated overlap contention bursts.
5. [x] Add deterministic dual-demo per-CPU `src:"MMIO"` parity checks between single-thread and multithread traces.
6. [x] Add deterministic dual-demo selected commit-line timing tuple checks (`t_start/t_end/stall`) across repeated runs.
7. [x] Add deterministic BIOS fixture checks for per-CPU `src` distributions across repeated runs.
8. [x] Add deterministic BIOS fixture checks for first MMIO_READ ordering relative to IFETCH commits.
9. [x] Expand SH-2 delay-slot matrix with BRA both-negative mixed-width overwrite plus follow-up target arithmetic checks.
10. [x] Expand SH-2 delay-slot matrix with RTS both-negative mixed-width overwrite plus follow-up target arithmetic checks.
11. [x] Add deterministic SH-2 overwrite test with two non-memory instructions between delay-slot and target store.
12. [x] Add deterministic commit-horizon regression for seven-cycle drains with alternating RAM/MMIO pressure.
13. [x] Add deterministic commit-horizon regression pinning values for four queued MMIO reads in one pending sequence.
14. [x] Add deterministic commit-horizon regression where CPU progress alternation reverses midway before convergence.
15. [x] Add deterministic trace-prefix assertions for the first 12 commit lines across repeated multithread runs.
16. [x] Run another code-review pass and refresh docs with any newly discovered risks/TODOs.

## Current 16-task batch (completed)

1. [x] Add deterministic SCU overlap lane tests combining opposite-lane halfword set with byte clear and IMS masking.
2. [x] Add deterministic SCU overlap regression for three-batch alternating set/clear contention with stable final source value.
3. [x] Add deterministic SCU overlap regression validating IST/source agreement after replayed IST clear writes.
4. [x] Add deterministic SCU write-log regression for stable per-CPU value histograms across repeated overlap bursts.
5. [x] Add deterministic dual-demo per-CPU `src:"READ"` parity checks under repeated multithread runs.
6. [x] Add deterministic dual-demo per-CPU `src:"BARRIER"` parity checks under repeated multithread runs.
7. [x] Add deterministic dual-demo commit-line timing tuple parity for selected MMIO_WRITE line across runs.
8. [x] Add deterministic BIOS fixture per-CPU kind parity checks for MMIO_READ/MMIO_WRITE across repeated runs.
9. [x] Expand SH-2 delay-slot matrix with BRA both-negative mixed-width overwrite plus dual target arithmetic instructions.
10. [x] Expand SH-2 delay-slot matrix with RTS both-negative mixed-width overwrite plus dual target arithmetic instructions.
11. [x] Add deterministic SH-2 same-address overwrite regression with three intermediate non-memory instructions.
12. [x] Add deterministic commit-horizon regression for eight-cycle mixed RAM/MMIO drains.
13. [x] Add deterministic commit-horizon regression pinning values for five queued MMIO reads in one sequence.
14. [x] Add deterministic commit-horizon regression where alternating progress updates reverse twice before convergence.
15. [x] Add deterministic trace-prefix assertions for the first 16 commit lines across repeated multithread runs.
16. [x] Run another code-review pass and refresh docs with newly discovered risks/TODOs.

## Current 16-task batch (completed)

1. [x] Add deterministic SCU overlap coverage for mixed byte writes targeting non-adjacent lanes in one batch.
2. [x] Add deterministic SCU overlap regression validating repeated source-clear writes remain idempotent across five runs.
3. [x] Add deterministic SCU overlap regression checking IST masked retention across alternating halfword IMS writes.
4. [x] Add deterministic SCU write-log regression for stable address histograms across repeated overlap bursts.
5. [x] Add deterministic dual-demo per-CPU `src:"IFETCH"` parity checks under repeated multithread runs.
6. [x] Add deterministic dual-demo selected IFETCH timing tuple parity checks across repeated multithread runs.
7. [x] Add deterministic dual-demo MMIO_READ timing tuple parity checks across repeated multithread runs.
8. [x] Add deterministic BIOS fixture per-CPU BARRIER count parity checks across repeated runs.
9. [x] Expand SH-2 delay-slot matrix with BRA both-negative overwrite plus target-side register copy before store.
10. [x] Expand SH-2 delay-slot matrix with RTS both-negative overwrite plus target-side register copy before store.
11. [x] Add deterministic SH-2 same-address overwrite regression with four intermediate non-memory instructions.
12. [x] Add deterministic commit-horizon regression for nine-cycle mixed RAM/MMIO drains.
13. [x] Add deterministic commit-horizon regression pinning values for six queued MMIO reads in one sequence.
14. [x] Add deterministic commit-horizon regression with alternating progress reversals on both CPUs before convergence.
15. [x] Add deterministic trace-prefix assertions for the first 20 commit lines across repeated multithread runs.
16. [x] Run another code-review pass and refresh docs with newly discovered risks/TODOs.

## Current 16-task batch (completed)

1. [x] Add deterministic SCU overlap regression for mixed-size byte/halfword writes targeting three lanes in one batch.
2. [x] Add deterministic SCU overlap regression validating repeated IST-clear and source-clear interleaving remains idempotent.
3. [x] Add deterministic SCU overlap regression for alternating IMS byte masks while source bits are concurrently set/cleared.
4. [x] Add deterministic SCU write-log regression verifying stable per-CPU address+value pair histograms across repeated bursts.
5. [x] Add deterministic dual-demo per-CPU `src:"READ"` timing tuple parity checks for selected READ lines.
6. [x] Add deterministic dual-demo per-CPU `src:"MMIO"` timing tuple parity checks for selected MMIO lines.
7. [x] Add deterministic dual-demo selected BARRIER timing tuple parity checks across repeated multithread runs.
8. [x] Add deterministic BIOS fixture per-CPU IFETCH timing tuple parity checks across repeated runs.
9. [x] Expand SH-2 delay-slot matrix with BRA both-negative overwrite plus target-side MOV Rm,Rn and ADD #imm before store.
10. [x] Expand SH-2 delay-slot matrix with RTS both-negative overwrite plus target-side MOV Rm,Rn and ADD #imm before store.
11. [x] Add deterministic SH-2 same-address overwrite regression with five intermediate non-memory instructions.
12. [x] Add deterministic commit-horizon regression for ten-cycle mixed RAM/MMIO drains.
13. [x] Add deterministic commit-horizon regression pinning values for seven queued MMIO reads in one sequence.
14. [x] Add deterministic commit-horizon regression with three alternating progress reversals on both CPUs before convergence.
15. [x] Add deterministic trace-prefix assertions for the first 24 commit lines across repeated multithread runs.
16. [x] Run another code-review pass and refresh docs with newly discovered risks/TODOs.

## Current 16-task batch (completed)

1. [x] Add deterministic SCU overlap scenario with three-lane mixed-size writes plus alternating clear masks in same sequence.
2. [x] Add deterministic SCU overlap scenario interleaving byte IMS masks with staggered source set/clear req_time values.
3. [x] Add deterministic SCU write-log regression for stable per-CPU lane-specific address histograms under mixed-size bursts.
4. [x] Expand SH-2 delay-slot matrix with BRA/RTS target-side MOV+ADD sequence plus additional target arithmetic.
5. [x] Add deterministic SH-2 same-address overwrite regression with six intermediate non-memory instructions.
6. [x] Add deterministic commit-horizon regression for eleven-cycle mixed RAM/MMIO drains.
7. [x] Add deterministic commit-horizon regression pinning values for eight queued MMIO reads in one sequence.
8. [x] Add deterministic commit-horizon regression with four alternating progress reversals on both CPUs before convergence.
9. [x] Add deterministic trace-order assertions for selected MMIO_READ/MMIO_WRITE/BARRIER line triplets in multithread runs.
10. [x] Add deterministic BIOS fixture parity checks for selected per-CPU MMIO timing tuples.
11. [x] Add deterministic BIOS fixture parity checks for selected per-CPU BARRIER timing tuples.
12. [x] Add focused TODO note + test scaffold for first DMA-produced bus op path (currently unmodeled).
13. [x] Add deterministic dual-demo per-CPU IFETCH timing tuple parity checks for selected IFETCH lines.
14. [x] Add deterministic dual-demo per-CPU READ/MMIO timing tuple parity checks for second-occurrence lines.
15. [x] Add deterministic trace-prefix assertions for the first 28 commit lines across repeated multithread runs.
16. [x] Run another code-review pass and refresh docs with newly discovered risks/TODOs.

## Next tasks

1. [x] Convert DMA TODO scaffold into first executable DMA-produced bus-op path test once minimal DMA submit API exists.
2. [x] Add deterministic trace assertions for first DMA-produced `src:"DMA"` MMIO commit timing/value tuple.
3. [x] Introduce a minimal bus-level owner/tag field for future DMA/SCU arbitration provenance in traces.
4. [x] Expand BIOS fixture to include one deterministic MMIO write/read pair routed through the future DMA path.
5. [x] Add focused regression for commit-horizon fairness when CPU and DMA producers contend on same MMIO address.
6. [x] Add first SMPC command write/read vertical-slice behavior beyond status-ready defaults.
7. [x] Add VDP1/SCU interrupt handoff scaffold with deterministic pending-bit assertions.
8. [x] Run another code-review pass and refresh docs with newly discovered risks/TODOs.

## Notes

- Keep all device semantics deterministic and test-backed.
- Prefer focused register-level behavior over broad partially-modeled subsystems.

## Next tasks (repopulated)

1. [x] Replace SH-2 mixed-width BRA/RTS overwrite TODO-guard expectations with architectural target-side overwrite semantics and focused regressions.
2. [x] Add centralized BusOp size validation in `BusArbiter`/MMIO path and regression checks for invalid-size rejection semantics.
3. [x] Add deterministic stress regression for `run_scripted_pair_multithread` progress/response liveness under extended contention windows.
4. [x] Refactor multithread scripted-pair coordination away from pure busy-wait yield loops toward deterministic bounded waiting.
5. [x] Expand VDP1->SCU interrupt scaffold from bridge register to first source-driven event path while preserving IMS/IST determinism assertions.
6. [x] Add trace-level assertions for VDP1->SCU handoff commits (`src`, `owner`, `tag`, timing tuple) under repeated runs.
7. [x] Run another code-review pass and refresh docs with newly discovered risks/TODOs.

## Next tasks (repopulated again, completed)

1. [x] Add deterministic VDP1 source-event status read-only/lane semantics regression coverage.
2. [x] Add trace-regression-level repeated-run stability checks for VDP1 source-event handoff commits.
3. [x] Connect VDP1 source-event trigger scaffold to first command/completion-producing path while preserving deterministic trace parity.
4. [x] Extend trace regression to pin VDP1 source-event status/IST timing tuples across single-thread/multithread scripted stress fixtures.
5. [x] Run another focused code-review pass and refresh docs with residual risks/TODOs.

## Next tasks (new 16-task batch)

1. [ ] Add deterministic regression for VDP1 command-status lane reads (byte + halfword) and verify packed busy/completion/command fields.
2. [ ] Add deterministic regression for VDP1 command completion clear/ack behavior via SCU IST clear sequences across repeated runs.
3. [ ] Extend VDP1 command-completion stress fixture with alternating CPU ownership and assert ST/MT trace parity.
4. [ ] Add deterministic trace assertions for VDP1 command completion commits carrying stable `src`/`owner`/`tag` metadata under repeated runs.
5. [ ] Add focused regression for enqueue-contract faults in multithread scripted stress under deliberate same-producer req_time inversion.
6. [ ] Add deterministic parity checks for halt-on-fault stop boundaries across ST/MT contention stress fixtures.
7. [ ] Expand BusArbiter invalid-size/invalid-alignment fault payload tests to pin encoded detail-class fields.
8. [ ] Add scripted CPU store-buffer stress test with repeated same-address writes + intermittent reads to pin bounded retire behavior.
9. [ ] Add deterministic TinyCache mismatch injection coverage in multithread mode ensuring fail-loud fault + parity with single-thread behavior.
10. [ ] Add SH-2 synthetic exception nested-entry guard test to pin deterministic trace markers and return ordering.
11. [ ] Expand SH-2 synthetic RTE coverage with altered SR masks to verify deterministic restore semantics.
12. [ ] Add deterministic BIOS trace assertion for selected MMIO_READ timing tuples involving SCU IST/IMS interactions.
13. [ ] Add deterministic dual-demo trace-prefix stability check for first 32 commit lines across repeated multithread runs.
14. [ ] Add deterministic regression for VDP1 event counter saturation/wrap policy (document chosen behavior and enforce with tests).
15. [ ] Refresh architecture/docs to describe VDP1 command/completion path assumptions and known hardware-accuracy gaps.
16. [ ] Run another full-project code-review pass and refresh docs with newly discovered risks/TODOs.
