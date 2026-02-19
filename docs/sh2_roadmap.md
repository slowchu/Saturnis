# SH-2 ISA Roadmap (100 Tasks / 16 Priority Groups)

This roadmap converts the current SH-2 follow-up plan into an executable checklist with rough sizing and dependency notes.

Legend:
- Priority: `P0` (highest) -> `P3` (lowest)
- Size: `S` (small), `M` (medium), `L` (large)
- Dependency tags:
  - `D-EXC` exception/TRAPA/RTE scaffolding
  - `D-BR` branch/delay-slot core behavior
  - `D-MEM` bus-backed memory op pipeline
  - `D-ALU` arithmetic/flag helpers
  - `D-TEST` testing helpers/infrastructure
  - `D-DOC` docs/coverage matrix

---

## Group 1 (P0): Exception/Return Correctness Hardening (7)

1. [ ] Add regression: `RTE` executes real non-memory delay-slot instruction before state restore. (`M`, deps: D-EXC, D-BR, D-TEST)
2. [ ] Add regression: `RTE` executes memory-op delay-slot before state restore. (`M`, deps: D-EXC, D-BR, D-MEM)
3. [ ] Assert stack pop order (`PC` then `SR`) for `RTE` in bus-commit sequence tests. (`S`, deps: D-EXC, D-TEST)
4. [ ] Add nested exception-entry regression with in-flight exception context. (`M`, deps: D-EXC, D-TEST)
5. [ ] Add multi-vector `TRAPA` regression with per-imm handler verification. (`M`, deps: D-EXC, D-MEM)
6. [ ] Document exception boundary contract (`pc_` meaning) inline near implementation. (`S`, deps: D-DOC)
7. [ ] Add trace-regression guard that new paths emit only `EXCEPTION_ENTRY/EXCEPTION_RETURN` markers. (`S`, deps: D-EXC, D-TEST)

## Group 2 (P0): Control-Flow ISA Gaps Blocking Firmware Paths (7)

8. [ ] Implement `BRAF Rm` (`pc_+4+Rm`) with deterministic delay-slot behavior. (`M`, deps: D-BR)
9. [ ] Implement `BSRF Rm` with PR update + delay slot. (`M`, deps: D-BR)
10. [ ] Add `BT/BF` boundary displacement tests (negative max, positive max). (`S`, deps: D-BR, D-TEST)
11. [ ] Add branch-in-delay-slot tests for new branch families under first-branch-wins policy. (`M`, deps: D-BR)
12. [ ] Add deterministic tests for branch-target formulas using fixed `pc_` baseline cases. (`S`, deps: D-BR, D-TEST)
13. [ ] Add per-family illegal-op rejection tests for unsupported branch encodings. (`S`, deps: D-TEST)
14. [ ] Add ST/MT parity tests for mixed branch + memory delay-slot sequences. (`M`, deps: D-BR, D-MEM, D-TEST)

## Group 3 (P0): Arithmetic/ALU Coverage for BIOS Routines (7)

15. [ ] Implement `ADDC Rm,Rn` with carry in/out via `T`. (`M`, deps: D-ALU)
16. [ ] Implement `ADDV Rm,Rn` with signed overflow into `T`. (`M`, deps: D-ALU)
17. [ ] Implement `NEGC Rm,Rn` with borrow semantics. (`M`, deps: D-ALU)
18. [ ] Add edge-case tests for `SUBC/SUBV` borrow/overflow chains. (`S`, deps: D-ALU, D-TEST)
19. [ ] Add fixed-vector ALU boundary matrix for carry/borrow/overflow transitions. (`M`, deps: D-ALU, D-TEST)
20. [ ] Add deterministic SR side-effect audit tests for immediate/logical ALU opcodes. (`S`, deps: D-ALU)
21. [ ] Add reference-vector checker scaffold (fixed corpus, deterministic). (`M`, deps: D-TEST)

## Group 4 (P0): Addressing-Mode Breadth for Real Code Paths (7)

22. [ ] Implement `@(R0,Rn)` indexed load forms (`MOV.B/W/L`). (`L`, deps: D-MEM)
23. [ ] Implement `@(R0,Rn)` indexed store forms (`MOV.B/W/L`). (`L`, deps: D-MEM)
24. [ ] Complete remaining `@(disp,GBR)` variants and validate scaling/sign-ext matrix. (`M`, deps: D-MEM)
25. [ ] Add aligned/unaligned deterministic behavior tests across new addressing forms. (`M`, deps: D-MEM, D-TEST)
26. [ ] Add test for mixed addressing-mode overwrite ordering at same address. (`M`, deps: D-MEM)
27. [ ] Resolve current MMIO-vs-RAM same-address TODO with expressible instruction path + regression. (`L`, deps: D-MEM, D-BR)
28. [ ] Add decode collision audit tests for addressing-form masks. (`S`, deps: D-TEST)

## Group 5 (P1): Shift/Bit Manipulation Expansion (6)

29. [ ] Implement `SHLL2`. (`S`, deps: D-ALU)
30. [ ] Implement `SHLL8`. (`S`, deps: D-ALU)
31. [ ] Implement `SHLL16`. (`S`, deps: D-ALU)
32. [ ] Implement `SHLR2`. (`S`, deps: D-ALU)
33. [ ] Implement `SHLR8`. (`S`, deps: D-ALU)
34. [ ] Implement `SHLR16`. (`S`, deps: D-ALU)

## Group 6 (P1): Multiply/Divide Instruction Family (6)

35. [ ] Implement `MULS.W`. (`M`, deps: D-ALU)
36. [ ] Implement `MULU.W`. (`M`, deps: D-ALU)
37. [ ] Implement `DMULS.L` into `MACH:MACL`. (`L`, deps: D-ALU)
38. [ ] Implement `DMULU.L` into `MACH:MACL`. (`L`, deps: D-ALU)
39. [ ] Implement `DIV0U`. (`M`, deps: D-ALU)
40. [ ] Implement `DIV1` deterministic step semantics + focused vector tests. (`L`, deps: D-ALU, D-TEST)

## Group 7 (P1): System/Register Transfer Completeness (6)

41. [ ] Implement missing `STC/LDC` register transfer forms required by firmware paths. (`M`, deps: D-EXC)
42. [ ] Implement missing `STS/LDS` transfer forms around PR/MAC regs where needed. (`M`, deps: D-EXC)
43. [ ] Add invariant tests: non-target GPRs unchanged for transfer ops. (`S`, deps: D-TEST)
44. [ ] Add reset-state tests for all visible control/accumulator registers. (`S`, deps: D-TEST)
45. [ ] Add deterministic tests for transfer + branch interleaving. (`M`, deps: D-BR, D-EXC)
46. [ ] Add TODO markers for any intentionally unmodeled SR mask-level side effects. (`S`, deps: D-DOC)

## Group 8 (P1): Memory Ordering + Determinism Validation (6)

47. [ ] Expand mixed-width same-address overwrite matrix for newly added forms. (`M`, deps: D-MEM)
48. [ ] Add dual-CPU contention tests for overlapping SH-2 memory writes. (`M`, deps: D-MEM, D-TEST)
49. [ ] Add deterministic stall-distribution assertions for new blocking ops. (`S`, deps: D-MEM)
50. [ ] Add byte-identical trace stability tests over repeated runs for new op paths. (`S`, deps: D-TEST)
51. [ ] Add long-prefix commit stability checks (64+ lines) for SH-2 stress fixtures. (`M`, deps: D-TEST)
52. [ ] Add aliasing tests (`m == n`) for all post-inc/pre-dec forms where architecturally special. (`S`, deps: D-MEM)

## Group 9 (P1): Decode Architecture Hardening (6)

53. [ ] Refactor opcode decode into helper groups to reduce mask overlap risk. (`L`, deps: D-TEST)
54. [ ] Add decode conflict regression corpus (adjacent encodings). (`M`, deps: D-TEST)
55. [ ] Add decode coverage test ensuring each implemented family has at least one vector. (`M`, deps: D-TEST)
56. [ ] Add unsupported-op corpus to assert deterministic `ILLEGAL_OP` faulting. (`S`, deps: D-TEST)
57. [ ] Add comments naming mnemonic for every mask pattern in decode chain. (`S`, deps: D-DOC)
58. [ ] Add static helper checks for encode/decode nibble extraction consistency. (`S`, deps: D-TEST)

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
70. [ ] Add microtest naming convention and template in docs for future contributors. (`S`, deps: D-DOC)

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

83. [ ] Add SH-2 instruction-driven MMIO tests for SCU/VDP/SMPC interactions using new addressing forms. (`M`, deps: D-MEM)
84. [ ] Validate byte-lane correctness for new byte/word MMIO write forms. (`M`, deps: D-MEM)
85. [ ] Add exception-entry interleave tests with pending MMIO side effects. (`M`, deps: D-EXC, D-MEM)
86. [ ] Add CPU-vs-DMA contention scenarios involving new SH-2 op paths. (`M`, deps: D-MEM)
87. [ ] Add trace provenance checks (`src/owner/tag`) for SH-2-triggered MMIO sequences. (`S`, deps: D-TEST)
88. [ ] Add deterministic replay tests for repeated device-trigger sequences under ST/MT. (`M`, deps: D-TEST)

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

Start with Groups 1 -> 4, then 5 -> 9, then the rest. This keeps correctness and BIOS-unblocking breadth ahead of refactors and lower-priority hardening.
