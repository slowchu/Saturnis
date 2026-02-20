ARCHIVED FOR BREVITY. DO NOT PURSUE
## Group 1 (P0): Exception/Return Correctness Hardening (7)

1. [x] Add regression: `RTE` executes real non-memory delay-slot instruction before state restore. (`M`, deps: D-EXC, D-BR, D-TEST)
2. [x] Add regression: `RTE` executes memory-op delay-slot before state restore. (`M`, deps: D-EXC, D-BR, D-MEM)
3. [x] Assert stack pop order (`PC` then `SR`) for `RTE` in bus-commit sequence tests. (`S`, deps: D-EXC, D-TEST)
4. [x] Add nested exception-entry regression with in-flight exception context. (`M`, deps: D-EXC, D-TEST)
5. [x] Add multi-vector `TRAPA` regression with per-imm handler verification. (`M`, deps: D-EXC, D-MEM)
6. [x] Document exception boundary contract (`pc_` meaning) inline near implementation. (`S`, deps: D-DOC)
7. [x] Add trace-regression guard that new paths emit only `EXCEPTION_ENTRY/EXCEPTION_RETURN` markers. (`S`, deps: D-EXC, D-TEST)

This is a revised executable checklist sized and tagged for Codex batching. It incorporates audit feedback:
- Adds **JMP/JSR/BSR** as P0 firmware blockers (and a **JSR decode bugfix + regression**).
- Elevates **SR + PR stack forms** needed by real exception handlers (STS.L/LDS.L PR, LDC/STC SR).
- Pulls **decode hardening** forward (mask overlap + adjacent-encoding corpus) to prevent repeat decode conflicts.
- Consolidates trivial “one-op-per-task” items (shifts, mul) into family tasks to free slots for higher-value coverage.
- Adds **PC-relative loads + MOVA** which are ubiquitous in BIOS/firmware.

Legend:
- Priority: `P0` (highest) → `P3` (lowest)
- Size: `S` (small), `M` (medium), `L` (large)
- Dependency tags:
  - `D-EXC` exception/TRAPA/RTE scaffolding
  - `D-BR` branch/delay-slot core behavior
  - `D-MEM` bus-backed memory op pipeline
  - `D-ALU` arithmetic/flag helpers
  - `D-TEST` testing helpers/infrastructure
  - `D-DOC` docs/coverage matrix

---

## Group 1 (P0): Exception/Return + Handler Prologue/Epilogue (7)

1. [x] Implement `STS.L PR,@-Rn` (exception/interrupt handler prologue) + regression fixture. (`M`, deps: D-EXC, D-MEM, D-TEST)
2. [x] Implement `LDS.L @Rm+,PR` (handler epilogue) + regression fixture. (`M`, deps: D-EXC, D-MEM, D-TEST)
3. [x] Implement `LDC Rm,SR` (incl. I-bit behavior) + regression that toggles interrupt mask deterministically. (`M`, deps: D-EXC, D-TEST)
4. [x] Implement `STC SR,Rn` + regression proving SR readback and non-target register invariants. (`S`, deps: D-EXC, D-TEST)
5. [x] Add regression: `RTE` executes **non-memory** delay-slot instruction before state restore. (`M`, deps: D-EXC, D-BR, D-TEST)
6. [x] Add regression: `RTE` executes **memory-op** delay-slot before state restore. (`M`, deps: D-EXC, D-BR, D-MEM, D-TEST)
7. [x] Assert stack pop order (`PC` then `SR`) for `RTE` in bus-commit sequence tests (and verify PR survives handler prologue/epilogue). (`S`, deps: D-EXC, D-TEST)

## Group 2 (P0): Core Control-Flow Firmware Blockers (7)

8. [x] Fix `JSR @Rn` decode/register extraction bug (ensure `n` nibble is extracted correctly) + regression using `JSR @R3` (non-R0). (`S`, deps: D-BR, D-TEST)
9. [x] Implement `JSR @Rn` (PR update + delay slot + deterministic PC update policy) + tests. (`M`, deps: D-BR, D-TEST)
10. [x] Implement `JMP @Rn` (delay slot + deterministic PC update policy) + tests. (`M`, deps: D-BR, D-TEST)
11. [x] Implement `BSR disp12` (PR update + delay slot + correct target formula) + tests. (`M`, deps: D-BR, D-TEST)
12. [x] Implement `BT/BF` **and** `BT/S` `BF/S` (if missing) with correct displacement sign-ext and delay-slot rules + tests. (`M`, deps: D-BR, D-TEST)
13. [x] Add boundary displacement tests for `BT/BF(/S)` and `BSR` (min/max negative/positive disp). (`S`, deps: D-BR, D-TEST)
14. [x] Add branch-in-delay-slot matrix tests across `{BRA,BSR,JMP,JSR,BT,BF}` × `{ALU,mem-op}` slots and ensure ST/MT parity. (`M`, deps: D-BR, D-MEM, D-TEST)

## Group 3 (P0): Decode Architecture Hardening (7)

15. [x] Refactor opcode decode into helper groups to reduce mask overlap risk (specific-before-generic). (`L`, deps: D-TEST)
16. [x] Add comments naming mnemonic + addressing form for every mask pattern in the decode chain. (`S`, deps: D-DOC)
17. [x] Introduce shared nibble/field extraction helpers (n/m/imm/disp) + unit tests (prevents `>>4` vs `>>8` class bugs). (`M`, deps: D-TEST)
18. [x] Add decode conflict regression corpus for adjacent encodings (include known historical collisions). (`M`, deps: D-TEST)
19. [x] Add **exclusivity test** over all 65536 opcodes: each opcode maps to **0 or 1** handler; overlaps fail deterministically. (`M`, deps: D-TEST)
20. [x] Add decode coverage test ensuring each implemented mnemonic family has at least one vector. (`M`, deps: D-TEST)
21. [x] Replace “unknown opcode → NOP” with deterministic `ILLEGAL_OP` faulting + corpus tests to lock behavior. (`S`, deps: D-TEST)

## Group 4 (P0): Addressing-Mode Breadth for Real Code Paths (7)

22. [x] Implement `MOV.W @(disp,PC),Rn` (PC-relative word load) with correct base and sign/zero extension rules + tests. (`L`, deps: D-MEM, D-TEST)
23. [x] Implement `MOV.L @(disp,PC),Rn` (PC-relative long load) with correct base/alignment rules + tests. (`L`, deps: D-MEM, D-TEST)
24. [x] Implement `MOVA @(disp,PC),R0` + tests (address compute patterns used by firmware). (`M`, deps: D-MEM, D-TEST)
25. [x] Implement `@(R0,Rn)` indexed load forms (`MOV.B/W/L`) + tests. (`L`, deps: D-MEM, D-TEST)
26. [x] Implement `@(R0,Rn)` indexed store forms (`MOV.B/W/L`) + tests. (`L`, deps: D-MEM, D-TEST)
27. [x] Complete remaining `@(disp,GBR)` variants and validate scaling/sign-ext matrix + tests. (`M`, deps: D-MEM, D-TEST)
28. [x] Add aligned/unaligned deterministic behavior tests + decode-collision audit tests for all new addressing forms. (`M`, deps: D-MEM, D-TEST)

---

## Group 5 (P1): Arithmetic/ALU Coverage for BIOS Routines (6)

29. [x] Implement `ADDC Rm,Rn` with carry in/out via `T`. (`M`, deps: D-ALU, D-TEST)
30. [x] Implement `ADDV Rm,Rn` with signed overflow into `T`. (`M`, deps: D-ALU, D-TEST)
31. [x] Implement `NEGC Rm,Rn` with borrow semantics via `T`. (`M`, deps: D-ALU, D-TEST)
32. [x] Add edge-case tests for `SUBC/SUBV` borrow/overflow chains (including multi-step carry chains). (`S`, deps: D-ALU, D-TEST)
33. [x] Add deterministic SR side-effect audit tests for core immediate/logical ops (`AND/OR/XOR/TST/CMP*`). (`S`, deps: D-ALU, D-TEST)
34. [x] Add reference-vector checker scaffold (fixed corpus, deterministic; used by future ALU tasks). (`M`, deps: D-TEST)

## Group 6 (P1): Shift/Bit Manipulation Expansion (6)

35. [x] Implement `SHLL2/SHLL8/SHLL16` as one family + vector tests. (`S`, deps: D-ALU, D-TEST)
36. [x] Implement `SHLR2/SHLR8/SHLR16` as one family + vector tests. (`S`, deps: D-ALU, D-TEST)
37. [x] Implement `SHAL/SHAR` (arithmetic shifts) + `T`-bit behavior tests. (`M`, deps: D-ALU, D-TEST)
38. [x] Implement `ROTL/ROTR` + `T`-bit behavior tests. (`M`, deps: D-ALU, D-TEST)
39. [x] Implement `ROTCL/ROTCR` (rotate through carry) + vector tests. (`M`, deps: D-ALU, D-TEST)
40. [x] Implement `SHAD/SHLD` (variable shifts) + vector tests for negative/large shift counts. (`L`, deps: D-ALU, D-TEST)

## Group 7 (P1): Multiply/Divide Instruction Family (6)

41. [x] Implement `MULS.W` + `MULU.W` as one family task + tests. (`M`, deps: D-ALU, D-TEST)
42. [x] Implement `DMULS.L` into `MACH:MACL` + tests. (`L`, deps: D-ALU, D-TEST)
43. [x] Implement `DMULU.L` into `MACH:MACL` + tests. (`L`, deps: D-ALU, D-TEST)
44. [x] Implement `DIV0U` + `DIV0S` + tests (status bit behavior). (`M`, deps: D-ALU, D-TEST)
45. [x] Implement `DIV1` deterministic step semantics + focused vector tests. (`L`, deps: D-ALU, D-TEST)
46. [x] Add `MAC.W/MAC.L` plan: either implement minimal deterministic behavior or explicitly `ILLEGAL_OP` with TODO+tests. (`M`, deps: D-ALU, D-TEST)

## Group 8 (P1): System/Register Transfer Completeness (6)

47. [ ] Implement `STC.L SR,@-Rn` (push SR) + tests. (`M`, deps: D-EXC, D-MEM, D-TEST)
48. [ ] Implement `LDC.L @Rm+,SR` (pop SR) + tests. (`M`, deps: D-EXC, D-MEM, D-TEST)
49. [ ] Implement `STC/LDC GBR` forms (+ `.L` stack forms if needed) + tests. (`M`, deps: D-EXC, D-MEM, D-TEST)
50. [ ] Implement `STC/LDC VBR` forms (+ `.L` stack forms if needed) + tests. (`M`, deps: D-EXC, D-MEM, D-TEST)
51. [ ] Implement `STS/LDS MACH/MACL` forms (+ `.L` stack forms if needed) + tests. (`M`, deps: D-EXC, D-MEM, D-TEST)
52. [ ] Add reset-state + non-target invariance test suite for all system/accumulator transfers. (`S`, deps: D-TEST)

## Group 9 (P1): Memory Ordering + Determinism Validation (6)

53. [ ] Expand mixed-width same-address overwrite matrix for newly added forms. (`M`, deps: D-MEM, D-TEST)
54. [ ] Add aliasing tests (`m == n`) for all post-inc/pre-dec forms (incl. `MOV.{B/W/L} @Rm+,Rn`). (`S`, deps: D-MEM, D-TEST)
55. [ ] Add dual-CPU contention tests for overlapping SH-2 memory writes (same addr, different widths). (`M`, deps: D-MEM, D-TEST)
56. [ ] Add deterministic stall-distribution assertions for any new blocking ops (if stall model is used). (`S`, deps: D-MEM, D-TEST)
57. [ ] Add byte-identical trace stability tests over repeated runs (ST vs MT) for new op paths. (`S`, deps: D-TEST)
58. [ ] Add long-prefix commit stability checks (64+ lines) for SH-2 stress fixtures. (`M`, deps: D-TEST)

---

## Group 10 (P2): BIOS Forward-Progress Instrumentation (6)

59. [ ] Add local script to run BIOS trace and capture first `ILLEGAL_OP` metrics. (`M`, deps: D-DOC)
60. [ ] Define local baseline artifact format (`count/opcode/pc`). (`S`, deps: D-DOC)
61. [ ] Add diff script to compare latest BIOS metric against baseline. (`S`, deps: D-DOC)
62. [ ] Add local report summarizing top unsupported opcodes encountered in trace. (`M`, deps: D-DOC)
63. [ ] Add developer note clarifying local-only nature (no BIOS in CI/artifacts). (`S`, deps: D-DOC)
64. [ ] Add deterministic parser tests for BIOS metric scripts (fixture-driven). (`S`, deps: D-TEST)

## Group 11 (P2): Testing Infrastructure Upgrades (6)

65. [ ] Introduce SH-2 opcode microtest harness with declarative vectors. (`L`, deps: D-TEST)
66. [ ] Add helper assertions for bus op sequence shape (read/write order, addr, size). (`S`, deps: D-TEST)
67. [ ] Add helper assertions for SR `T`-bit transitions. (`S`, deps: D-TEST)
68. [ ] Split monolithic SH-2 tests into themed clusters (alu/branch/mem/exception). (`M`, deps: D-TEST)
69. [ ] Add deterministic “golden state” checkpoints for longer control-flow fixtures. (`M`, deps: D-TEST)
70. [ ] Add microtest naming convention + template in docs for future contributors. (`S`, deps: D-DOC)

## Group 12 (P2): Documentation and Coverage Tracking (6)

71. [ ] Create machine-readable SH-2 coverage matrix (`implemented/tested/todo`). (`M`, deps: D-DOC)
72. [ ] Add architecture section mapping supported op families to behavioral constraints. (`S`, deps: D-DOC)
73. [ ] Add explicit deviations list vs full SH-2 hardware behavior. (`S`, deps: D-DOC)
74. [ ] Link each TODO opcode family to at least one failing/placeholder test or note. (`S`, deps: D-DOC)
75. [ ] Add release checklist item to refresh matrix + docs per ISA batch. (`S`, deps: D-DOC)
76. [ ] Keep docs/todo synchronized with matrix to prevent stale completion claims. (`S`, deps: D-DOC)

## Group 13 (P2): Performance-Safe Correctness (6)

77. [ ] Add deterministic microbench instruction streams (functional checks only, no wall clock). (`M`, deps: D-TEST)
78. [ ] Audit new opcode paths for unintended extra bus operations. (`S`, deps: D-MEM)
79. [ ] Add runahead-budget invariance tests for new instruction families. (`M`, deps: D-TEST)
80. [ ] Add cache hit/miss parity checks for new fetch+execute patterns. (`M`, deps: D-TEST)
81. [ ] Add memory-op latency consistency checks in trace tuples for fixed fixtures. (`S`, deps: D-TEST)
82. [ ] Add deterministic stress fixture mixing ALU-heavy and memory-heavy op runs. (`M`, deps: D-TEST)

## Group 14 (P3): Device/CPU Interaction Robustness (6)

83. [ ] Resolve MMIO-vs-RAM same-address ordering TODO with an expressible instruction path + regression. (`L`, deps: D-MEM, D-BR, D-TEST)
84. [ ] Add instruction-driven MMIO tests for SCU/VDP/SMPC interactions using new addressing forms. (`M`, deps: D-MEM, D-TEST)
85. [ ] Validate byte-lane correctness for new byte/word MMIO write forms. (`M`, deps: D-MEM, D-TEST)
86. [ ] Add exception-entry interleave tests with pending MMIO side effects. (`M`, deps: D-EXC, D-MEM, D-TEST)
87. [ ] Add CPU-vs-DMA contention scenarios involving new SH-2 op paths (once DMA exists). (`M`, deps: D-MEM, D-TEST)
88. [ ] Add deterministic replay tests for repeated device-trigger sequences under ST/MT (include provenance checks). (`M`, deps: D-TEST)

## Group 15 (P3): Refactor and Maintainability Debt (6)

89. [ ] Extract exception/TRAPA/RTE multi-step state machine into dedicated helper functions. (`M`, deps: D-EXC)
90. [ ] Consolidate sign-extension helpers to avoid duplicated conversion code. (`S`, deps: D-ALU)
91. [ ] Consolidate displacement/scaling helpers for PC/GBR/Rn forms. (`S`, deps: D-MEM)
92. [ ] Remove obsolete synthetic-path state if fully replaced by real paths. (`M`, deps: D-EXC)
93. [ ] Add focused inline comments for tricky delay-slot restoration rules. (`S`, deps: D-DOC)
94. [ ] Add regression IDs in test names/comments for easier bisecting. (`S`, deps: D-TEST)

## Group 16 (P3): Long-Horizon Completeness and Quality Gates (6)

95. [ ] Implement next unsupported opcode family from BIOS fault histogram (highest frequency first). (`L`, deps: D-DOC)
96. [ ] Close current SH-2 TODO hotspots in tests with real implementations/regressions. (`M`, deps: D-TEST)
97. [ ] Define quantitative SH-2 coverage milestone gate (e.g., target subset completeness). (`S`, deps: D-DOC)
98. [ ] Add dashboard/summary file tracking milestone progress by group. (`S`, deps: D-DOC)
99. [ ] Run deterministic multi-run campaign after each group completion and log parity status. (`M`, deps: D-TEST)
100. [ ] Freeze “Tier-2 ISA completeness” definition and publish execution order for subsequent sprints. (`S`, deps: D-DOC)

---

## Recommended immediate execution order

1) Group 2 (control-flow blockers) **in parallel with** Group 1 (handler prologue/epilogue + SR/PR correctness).  
2) Group 3 (decode hardening) **before** expanding breadth in Groups 4–9.  
3) Group 4 (PC-relative + indexed/GBR addressing) next — it unlocks real firmware patterns.  
4) Then Groups 5–9 in any order, guided by BIOS unsupported-op histogram (Group 10) once available.
