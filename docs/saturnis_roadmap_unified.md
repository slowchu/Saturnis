# Saturnis Roadmap — Deterministic Sega Saturn Emulator
*Unified document: high-level gates + executable task lists.*
*SH-2 task groups are embedded under Gate C. All other gates will receive equivalent task expansions as execution plans are written.*

---

> ⚠️ **Global prerequisite for Gates G, H, and I:**
> The title requirements matrices defined in **J-pre** must be completed before the scope and exit criteria of Gates G, H, and I are treated as final. Until those matrices exist in the repo, all feature depth in G/H/I is provisional. Do not begin scoping or tasking G/H/I work as fully specified until J-pre is closed.

---

## 0) North Star

### v1.0 Acceptance Criteria

**Boot path**
- Boots external BIOS (user-supplied) to a named checkpoint: the first stable controller-poll loop reached after BIOS hands off (i.e., the BIOS is displaying its top-level UI and polling for input). This is the concrete pass/fail moment — not "reaches some BIOS code" but "reaches the input-polling loop without faulting or hanging."
- Boots 5 selected commercial titles to gameplay (titles defined in J-pre/J1). "Gameplay" means the title has passed its title screen and the player has control, as defined per-title in the J-pre requirements matrices.

**Determinism**
- Same inputs + same settings ⇒ identical trace hash over a defined window.
- Determinism windows (these are the enforced numbers; revise by explicit roadmap update, not silently):
  - **Frames:** 300 frames (5 seconds at 60fps) from a defined start state.
  - **Audio:** 240,000 samples (5 seconds at 48kHz) from the same start state.
  - **Trace:** Full trace hash over the same 300-frame window.
- At the BIOS checkpoint and each title's gameplay checkpoint: frame hash and audio hash must be identical across runs and across machines with the same config.

**Policy**
- No per-title hacks per the policy below.

### "No Per-Title Hacks" Policy

**Allowed (global)**
- Global user settings: renderer backend, audio backend, vsync, interpolation, controller mapping.
- Global accuracy modes that apply uniformly (e.g., "strict timing" vs "fast timing").
- Correctness fixes that improve behavior across titles.

**Disallowed (hacks)**
- Per-title MMIO overrides ("if title X reads reg Y, return magic Z").
- Per-title timing fudge factors ("for title X, delay DMA by N / shift VBlank by N").
- Per-title renderer special cases ("for title X, treat bitfield differently").

**Temporary compatibility shims (optional escape hatch)**
- If allowed: must be tagged `SHIM:` + linked issue + explicit removal milestone.

### BIOS Policy
- v1.0 uses external BIOS; any HLE is dev-only, clearly labeled, and off by default.

---

## 1) Core Invariants (Do Not Compromise)

- **Trace-first development:** meaningful MMIO/device behavior must be trace-visible.
- **Single authoritative scheduler/timebase** once timed subsystems exist together (scanlines/VBlank, DMA progression, audio cadence, CD cadence).
- **Deterministic ordering contract:** bus total-order rule is explicit, versioned, and tested.

---

## A) Test Harness + Trace Discipline (Gate A0)

**Goal:** Make progress measurable and regressions cheap.

### Deliverables
- Deterministic replay mode (record inputs + deterministic seeds; replay bit-identically).
- Trace schema versioning (`trace_version`) + fixture update rules.
- "First divergence" diff tooling (trace → pinpoint earliest mismatch).
- Contract tests for arbitration invariants (tie-break determinism, horizon rules, progress constraints).

### Exit Criteria
- `ctest` stable across 3 repeated runs (same machine/config).
- Trace fixtures reproduce identically (hash-stable).

---

## B) Boot Foundation: Scheduler + SMPC Orchestration (Gate B0)

### B1) Authoritative Scheduler (Foundation, Not Optional)

**Why:** Integration of VBlank/scanlines, DMA, audio cadence, and CD cadence requires a single time authority; real Saturn library flows assume VBlank-driven sequencing and completion gating (including VDP1 draw-end-related frame-change modes).

**Deliverables**
- Scheduler owns emulated time and dispatches:
  - CPU execution slices
  - VBlank in/out + scanline ticks (stub OK initially)
  - DMA step events (stub OK initially)
  - Audio sample-block events (stub OK initially)
  - CD periodic cadence events (stub OK initially; concretized in I2)

**Exit Criteria**
- Stub "run N frames" produces identical trace hash across runs.

---

### B2) SMPC Boot Orchestration (Early; It Gates CPU Reality)

**Why:** SMPC is explicitly "System Manager & Peripheral Control," including system reset control and peripheral interfaces.

**Minimum bring-up set**
- Command flow sufficient for early orchestration:
  - `SYSRES`
  - `MSHON` / `SSHON`
  - `SNDON` / `SNDOFF`
  - `CDON` / `CDOFF` (as you model CD block enable)
- Tie SMPC command timing to scheduler/VBlank windows (trace violations).

**Exit Criteria**
- Master/slave bring-up state is not hardcoded; it flows through SMPC paths.
- Trace shows SMPC events deterministically.

**Module:** `src/dev/smpc.*` must exist by this gate.

---

## C) SH-2 CPU Correctness (Gate C0)

*This gate is fully expanded with an executable task list. Tasks are grouped by priority (P0→P3) and tagged with dependency codes. Recommended execution order is at the end of this section.*

**Dependency tag legend:**
- `D-EXC` — exception/TRAPA/RTE scaffolding
- `D-BR` — branch/delay-slot core behavior
- `D-MEM` — bus-backed memory op pipeline
- `D-ALU` — arithmetic/flag helpers
- `D-TEST` — testing helpers/infrastructure
- `D-DOC` — docs/coverage matrix

**Size legend:** `S` = small, `M` = medium, `L` = large

---

### C-pre / Group 1 (P0): Exception/Return + Handler Prologue/Epilogue
*These are "silent killers": they corrupt state without immediate crashes. Must land before BIOS checkpoint work.*

1. [x] Implement `STS.L PR,@-Rn` (exception/interrupt handler prologue) + regression fixture. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
2. [x] Implement `LDS.L @Rm+,PR` (handler epilogue) — fix post-increment semantics (`PR = ReadLong(Rm); Rm += 4`) + regression. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
3. [x] Implement `LDC Rm,SR` (incl. I-bit behavior) + regression that toggles interrupt mask deterministically. (`M`, `D-EXC`, `D-TEST`)
4. [x] Implement `STC SR,Rn` + regression proving SR readback and non-target register invariants. (`S`, `D-EXC`, `D-TEST`)
5. [x] Add regression: `RTE` executes **non-memory** delay-slot instruction before state restore. (`M`, `D-EXC`, `D-BR`, `D-TEST`)
6. [x] Add regression: `RTE` executes **memory-op** delay-slot before state restore. (`M`, `D-EXC`, `D-BR`, `D-MEM`, `D-TEST`)
7. [x] Assert stack pop order (`PC` then `SR`) for `RTE` in bus-commit sequence tests (and verify PR survives handler prologue/epilogue). (`S`, `D-EXC`, `D-TEST`)

**Exit criteria:** `tests/test_sh2_known_bugs.cpp` (or equivalent) exists and is CI-required. One BIOS-like sequence test that fails without the fixes.

---

### Group 2 (P0): Core Control-Flow Firmware Blockers

8. [x] Fix `JSR @Rn` decode/register extraction bug (ensure `n` nibble is extracted correctly) + regression using `JSR @R3` (non-R0). (`S`, `D-BR`, `D-TEST`)
9. [x] Implement `JSR @Rn` (PR update + delay slot + deterministic PC update policy) + tests. (`M`, `D-BR`, `D-TEST`)
10. [x] Implement `JMP @Rn` (delay slot + deterministic PC update policy) + tests. (`M`, `D-BR`, `D-TEST`)
11. [x] Implement `BSR disp12` (PR update + delay slot + correct target formula) + tests. (`M`, `D-BR`, `D-TEST`)
12. [x] Implement `BT/BF` and `BT/S` `BF/S` (if missing) with correct displacement sign-ext and delay-slot rules + tests. (`M`, `D-BR`, `D-TEST`)
13. [x] Add boundary displacement tests for `BT/BF(/S)` and `BSR` (min/max negative/positive disp). (`S`, `D-BR`, `D-TEST`)
14. [x] Add branch-in-delay-slot matrix tests across `{BRA,BSR,JMP,JSR,BT,BF}` × `{ALU,mem-op}` slots and ensure ST/MT parity. (`M`, `D-BR`, `D-MEM`, `D-TEST`)

---

### Group 3 (P0): Decode Architecture Hardening
*Must complete before expanding breadth in Groups 4–9.*

15. [x] Refactor opcode decode into helper groups to reduce mask overlap risk (specific-before-generic). (`L`, `D-TEST`)
16. [x] Add comments naming mnemonic + addressing form for every mask pattern in the decode chain. (`S`, `D-DOC`)
17. [x] Introduce shared nibble/field extraction helpers (n/m/imm/disp) + unit tests (prevents `>>4` vs `>>8` class bugs). (`M`, `D-TEST`)
18. [x] Add decode conflict regression corpus for adjacent encodings (include known historical collisions). (`M`, `D-TEST`)
19. [x] Add **exclusivity test** over all 65536 opcodes: each opcode maps to **0 or 1** handler; overlaps fail deterministically. (`M`, `D-TEST`)
20. [x] Add decode coverage test ensuring each implemented mnemonic family has at least one vector. (`M`, `D-TEST`)
21. [x] Replace "unknown opcode → NOP" with deterministic `ILLEGAL_OP` faulting + corpus tests to lock behavior. (`S`, `D-TEST`)

**Exit criteria:** Decode ambiguity corpus is green and CI-gated. Decode conflict regression corpus pinned.

---

### Group 4 (P0): Addressing-Mode Breadth for Real Code Paths

22. [x] Implement `MOV.W @(disp,PC),Rn` (PC-relative word load) with correct base and sign/zero extension rules + tests. (`L`, `D-MEM`, `D-TEST`)
23. [x] Implement `MOV.L @(disp,PC),Rn` (PC-relative long load) with correct base/alignment rules + tests. (`L`, `D-MEM`, `D-TEST`)
24. [x] Implement `MOVA @(disp,PC),R0` + tests (address compute patterns used by firmware). (`M`, `D-MEM`, `D-TEST`)
25. [x] Implement `@(R0,Rn)` indexed load forms (`MOV.B/W/L`) + tests. (`L`, `D-MEM`, `D-TEST`)
26. [x] Implement `@(R0,Rn)` indexed store forms (`MOV.B/W/L`) + tests. (`L`, `D-MEM`, `D-TEST`)
27. [x] Complete remaining `@(disp,GBR)` variants and validate scaling/sign-ext matrix + tests. (`M`, `D-MEM`, `D-TEST`)
28. [x] Add aligned/unaligned deterministic behavior tests + decode-collision audit tests for all new addressing forms. (`M`, `D-MEM`, `D-TEST`)

---

### Group 5 (P1): Arithmetic/ALU Coverage for BIOS Routines

29. [x] Implement `ADDC Rm,Rn` with carry in/out via `T`. (`M`, `D-ALU`, `D-TEST`)
30. [x] Implement `ADDV Rm,Rn` with signed overflow into `T`. (`M`, `D-ALU`, `D-TEST`)
31. [x] Implement `NEGC Rm,Rn` with borrow semantics via `T`. (`M`, `D-ALU`, `D-TEST`)
32. [x] Add edge-case tests for `SUBC/SUBV` borrow/overflow chains (including multi-step carry chains). (`S`, `D-ALU`, `D-TEST`)
33. [x] Add deterministic SR side-effect audit tests for core immediate/logical ops (`AND/OR/XOR/TST/CMP*`). (`S`, `D-ALU`, `D-TEST`)
34. [x] Add reference-vector checker scaffold (fixed corpus, deterministic; used by future ALU tasks). (`M`, `D-TEST`)

---

### Group 6 (P1): Shift/Bit Manipulation Expansion

35. [x] Implement `SHLL2/SHLL8/SHLL16` as one family + vector tests. (`S`, `D-ALU`, `D-TEST`)
36. [x] Implement `SHLR2/SHLR8/SHLR16` as one family + vector tests. (`S`, `D-ALU`, `D-TEST`)
37. [x] Implement `SHAL/SHAR` (arithmetic shifts) + `T`-bit behavior tests. (`M`, `D-ALU`, `D-TEST`)
38. [x] Implement `ROTL/ROTR` + `T`-bit behavior tests. (`M`, `D-ALU`, `D-TEST`)
39. [x] Implement `ROTCL/ROTCR` (rotate through carry) + vector tests. (`M`, `D-ALU`, `D-TEST`)
40. [x] Implement `SHAD/SHLD` (variable shifts) + vector tests for negative/large shift counts. (`L`, `D-ALU`, `D-TEST`)

---

### Group 7 (P1): Multiply/Divide Instruction Family

41. [x] Implement `MULS.W` + `MULU.W` as one family task + tests. (`M`, `D-ALU`, `D-TEST`)
42. [x] Implement `DMULS.L` into `MACH:MACL` + tests. (`L`, `D-ALU`, `D-TEST`)
43. [x] Implement `DMULU.L` into `MACH:MACL` + tests. (`L`, `D-ALU`, `D-TEST`)
44. [x] Implement `DIV0U` + `DIV0S` + tests (status bit behavior). (`M`, `D-ALU`, `D-TEST`)
45. [x] Implement `DIV1` deterministic step semantics + focused vector tests. (`L`, `D-ALU`, `D-TEST`)
46. [x] Add `MAC.W/MAC.L` plan: either implement minimal deterministic behavior or explicitly `ILLEGAL_OP` with TODO + tests. (`M`, `D-ALU`, `D-TEST`)

---

### Group 8 (P1): System/Register Transfer Completeness

47. [x] Implement `STC.L SR,@-Rn` (push SR) + tests. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
48. [x] Implement `LDC.L @Rm+,SR` (pop SR) + tests. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
49. [x] Implement `STC/LDC GBR` forms (+ `.L` stack forms if needed) + tests. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
50. [x] Implement `STC/LDC VBR` forms (+ `.L` stack forms if needed) + tests. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
51. [x] Implement `STS/LDS MACH/MACL` forms (+ `.L` stack forms if needed) + tests. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
52. [x] Add reset-state + non-target invariance test suite for all system/accumulator transfers. (`S`, `D-TEST`)

---

### Group 9 (P1): Memory Ordering + Determinism Validation

53. [ ] Expand mixed-width same-address overwrite matrix for newly added forms. (`M`, `D-MEM`, `D-TEST`)
54. [ ] Add aliasing tests (`m == n`) for all post-inc/pre-dec forms (incl. `MOV.{B/W/L} @Rm+,Rn`). (`S`, `D-MEM`, `D-TEST`)
55. [ ] Add dual-CPU contention tests for overlapping SH-2 memory writes (same addr, different widths). (`M`, `D-MEM`, `D-TEST`)
56. [ ] Add deterministic stall-distribution assertions for any new blocking ops (if stall model is used). (`S`, `D-MEM`, `D-TEST`)
57. [ ] Add byte-identical trace stability tests over repeated runs (ST vs MT) for new op paths. (`S`, `D-TEST`)
58. [ ] Add long-prefix commit stability checks (64+ lines) for SH-2 stress fixtures. (`M`, `D-TEST`)

**Exit criteria (C1–C2):** BIOS checkpoint reached without relying on undefined delay-slot behavior. ISA gaps for selected v1.0 titles (see J-pre matrices) are closed.

---

### Group 10 (P2): BIOS Forward-Progress Instrumentation

59. [ ] Add local script to run BIOS trace and capture first `ILLEGAL_OP` metrics. (`M`, `D-DOC`)
60. [ ] Define local baseline artifact format (`count/opcode/pc`). (`S`, `D-DOC`)
61. [ ] Add diff script to compare latest BIOS metric against baseline. (`S`, `D-DOC`)
62. [ ] Add local report summarizing top unsupported opcodes encountered in trace. (`M`, `D-DOC`)
63. [ ] Add developer note clarifying local-only nature (no BIOS in CI/artifacts). (`S`, `D-DOC`)
64. [ ] Add deterministic parser tests for BIOS metric scripts (fixture-driven). (`S`, `D-TEST`)

---

### Group 11 (P2): Testing Infrastructure Upgrades

65. [ ] Introduce SH-2 opcode microtest harness with declarative vectors. (`L`, `D-TEST`)
66. [ ] Add helper assertions for bus op sequence shape (read/write order, addr, size). (`S`, `D-TEST`)
67. [ ] Add helper assertions for SR `T`-bit transitions. (`S`, `D-TEST`)
68. [ ] Split monolithic SH-2 tests into themed clusters (alu/branch/mem/exception). (`M`, `D-TEST`)
69. [ ] Add deterministic "golden state" checkpoints for longer control-flow fixtures. (`M`, `D-TEST`)
70. [ ] Add microtest naming convention + template in docs for future contributors. (`S`, `D-DOC`)

---

### Group 12 (P2): Documentation and Coverage Tracking

71. [ ] Create machine-readable SH-2 coverage matrix (`implemented/tested/todo`). (`M`, `D-DOC`)
72. [ ] Add architecture section mapping supported op families to behavioral constraints. (`S`, `D-DOC`)
73. [ ] Add explicit deviations list vs full SH-2 hardware behavior. (`S`, `D-DOC`)
74. [ ] Link each TODO opcode family to at least one failing/placeholder test or note. (`S`, `D-DOC`)
75. [ ] Add release checklist item to refresh matrix + docs per ISA batch. (`S`, `D-DOC`)
76. [ ] Keep `docs/todo` synchronized with matrix to prevent stale completion claims. (`S`, `D-DOC`)

---

### Group 13 (P2): Cache / Uncached Alias Model + Performance-Safe Correctness

*Maps to C3 in the high-level gate.*

77. [ ] Define and document cache/uncached alias visibility contract (even if coarse). (`M`, `D-MEM`, `D-DOC`)
78. [ ] Add alias behavior tests: uncached alias forces bus-visible order in trace. (`M`, `D-MEM`, `D-TEST`)
79. [ ] Add deterministic microbench instruction streams (functional checks only, no wall clock). (`M`, `D-TEST`)
80. [ ] Audit new opcode paths for unintended extra bus operations. (`S`, `D-MEM`)
81. [ ] Add runahead-budget invariance tests for new instruction families. (`M`, `D-TEST`)
82. [ ] Add cache hit/miss parity checks for new fetch+execute patterns. (`M`, `D-TEST`)

**Exit criteria (C3):** Alias tests demonstrate correct trace-visible ordering.

---

### Group 14 (P3): Device/CPU Interaction Robustness

83. [ ] Resolve MMIO-vs-RAM same-address ordering TODO with an expressible instruction path + regression. (`L`, `D-MEM`, `D-BR`, `D-TEST`)
84. [ ] Add instruction-driven MMIO tests for SCU/VDP/SMPC interactions using new addressing forms. (`M`, `D-MEM`, `D-TEST`)
85. [ ] Validate byte-lane correctness for new byte/word MMIO write forms. (`M`, `D-MEM`, `D-TEST`)
86. [ ] Add exception-entry interleave tests with pending MMIO side effects. (`M`, `D-EXC`, `D-MEM`, `D-TEST`)
87. [ ] Add CPU-vs-DMA contention scenarios involving new SH-2 op paths (once DMA exists). (`M`, `D-MEM`, `D-TEST`)
88. [ ] Add deterministic replay tests for repeated device-trigger sequences under ST/MT (include provenance checks). (`M`, `D-TEST`)

---

### Group 15 (P3): Refactor and Maintainability Debt

89. [ ] Extract exception/TRAPA/RTE multi-step state machine into dedicated helper functions. (`M`, `D-EXC`)
90. [ ] Consolidate sign-extension helpers to avoid duplicated conversion code. (`S`, `D-ALU`)
91. [ ] Consolidate displacement/scaling helpers for PC/GBR/Rn forms. (`S`, `D-MEM`)
92. [ ] Remove obsolete synthetic-path state if fully replaced by real paths. (`M`, `D-EXC`)
93. [ ] Add focused inline comments for tricky delay-slot restoration rules. (`S`, `D-DOC`)
94. [ ] Add regression IDs in test names/comments for easier bisecting. (`S`, `D-TEST`)

---

### Group 16 (P3): Long-Horizon Completeness and Quality Gates

95. [ ] Implement next unsupported opcode family from BIOS fault histogram (highest frequency first). (`L`, `D-DOC`)
96. [ ] Close current SH-2 TODO hotspots in tests with real implementations/regressions. (`M`, `D-TEST`)
97. [ ] Define quantitative SH-2 coverage milestone gate (e.g., target subset completeness %). (`S`, `D-DOC`)
98. [ ] Add dashboard/summary file tracking milestone progress by group. (`S`, `D-DOC`)
99. [ ] Run deterministic multi-run campaign after each group completion and log parity status. (`M`, `D-TEST`)
100. [ ] Freeze "Tier-2 ISA completeness" definition and publish execution order for subsequent sprints. (`S`, `D-DOC`)

---

### Gate C0 — Recommended Task Execution Order

1. **Groups 1 + 2 in parallel** — handler prologue/epilogue + control-flow blockers.
2. **Group 3** — decode hardening before any further breadth expansion.
3. **Group 4** — PC-relative + indexed/GBR addressing (unlocks real firmware patterns).
4. **Groups 5–9** in any order, guided by BIOS unsupported-op histogram (Group 10) once available.
5. **Groups 10–13** — infrastructure, instrumentation, and cache model.
6. **Groups 14–16** — device interaction, refactor debt, and long-horizon completeness.

---

## D) Memory Map + MMIO Routing + Module Boundaries (Gate D0)

**Goal:** Correct address routing and enforce code ownership boundaries before devices expand.

### Deliverables
- Saturn address routing table (RAM vs MMIO) documented and implemented.
- MMIO read/write tracing complete (addr, width, device, side-effect class).
- Endianness and access-width rules consistent across CPU ↔ bus ↔ memory.

**Hard deliverable: `devices.*` must never become a god-object**
- `src/dev/devices.*` is router-only: registration/reset wiring + address decode + dispatch.
- No device behavioral state machines in `devices.*` beyond trivial forwarding.

### Module Map (Enforceable; By-Gate Existence)

| Module | Must exist by |
|---|---|
| `src/dev/smpc.*` | Gate B0 |
| `src/dev/scu_irq.*`, `src/dev/scu_dma.*` | Gate F0 |
| `src/video/vdp1.*`, `src/video/vdp2.*` | Gate G0 |
| `src/cpu/m68k_*` (or wrapped core), `src/audio/scsp.*` | Gate H0 |
| `src/cd/cd_block.*` | Gate I0 |

### Exit Criteria
- `devices.*` contains only dispatch; device logic lives in the modules above.
- Every MMIO region routes to a named module (even if stubbed).

---

## E) Bus Arbitration + Contention Model (Gate E0)

**Goal:** Freeze the determinism contract and make it adversarially testable.

### Deliverables
- Generalize to N producers (master SH-2, slave SH-2, SCU DMA, later CD, etc.).
- Declare total ordering rule (e.g., `req_time` → `priority` → `producer_seq`).
- Horizon/safe-progress rules defined and enforced.

### Exit Criteria (Stress the Right Thing)
Property/stress tests over randomized producer op streams:
- Timestamp skew (one producer ahead/behind)
- Exact ties + tie streaks
- Same-address R/W conflicts
- Mixed widths (8/16/32) + alignment edges

Assertions:
- Deterministic total order matches declared policy
- Horizon safety never violated
- Progress guarantees hold (no deadlock under admissible streams)
- Hand-crafted "adversarial corpus" pinned

---

## F) SCU: IRQ Routing + DMA (Gate F0)

### Deliverables
- SCU interrupt controller semantics (mask/pending/clear/set) integrated with SH-2 IRQ entry.
- DMA engine MVP with deterministic ordering vs CPU bus ops.

**Boundary enforcement (non-negotiable)**
- All SCU behavior lives in `scu_irq.*` and `scu_dma.*`.
- `devices.*` remains router-only (review rule).

### Exit Criteria
- DMA harness produces deterministic trace including transfer progress + IRQ behavior.
- No SCU logic lands in `devices.*`.

---

## G) Video: VDP2 + VDP1 with Explicit Composition + Timing (Gate G0)

> ⚠️ **J-pre prerequisite:** Before locking scope for Gates G, H, and I, complete the J-pre requirements matrices. Feature depth and exit criteria in these gates are provisional until the matrices are checked into the repo and reviewed. Do not treat the deliverables below as fully specified until that is done.

**Why this section is structured this way:**
VDP1 draws sprites/polygons into a framebuffer consumed by VDP2 for display. VDP1 uses a front/back framebuffer model: one displayed, one drawn into. Sega's library documentation includes frame-change modes that explicitly confirm VDP1 draw end before frame change in certain modes.

### G1) VDP2 Baseline (Display Timing + Deterministic Output)

**Deliverables**
- VBlank/scanline events integrated with scheduler.
- Deterministic frame hash over N frames (even if rendering is minimal).
- VDP2 can source "sprite framebuffer data received from VDP1" (stubbed source initially).

**Exit Criteria:** Micro-test produces stable frame hashes + trace-visible scanline/VBlank events.

---

### G1.5) VDP1↔VDP2 Composition Contract (Load-Bearing)

**Deliverables**
- VDP1 framebuffer ownership model: explicit "displayed buffer" vs "draw target" concept.
- VDP2 sprite layer consumption rule: VDP2 must consume the completed/displayed VDP1 buffer, never an in-progress draw target.
- Frame-change policy stub: trace-visible modes consistent with "change on VBlank" vs "change gated by draw end" patterns from Sega's library documentation.

**Exit Criteria:** Composition test demonstrates VDP2 always displays the prior completed buffer while VDP1 is still drawing (or waits in the relevant synchronous mode), never a partially updated buffer.

---

### G2) VDP1 Command Execution MVP

**Deliverables**
- Command list parsing + subset rendering into framebuffer.
- Draw-end/completion event is trace-visible.

**Exit Criteria:** Deterministic draw-end signal exists and composes correctly via G1.5.

---

### G3) VDP1 Timing Model Milestone

**Why:** Real Saturn programming patterns explicitly wait for VDP1 draw end inside VBlank routines (e.g., `SPR_WaitDrawEnd()` usage), meaning "draw completion timing" is a synchronization signal, not just a render detail.

**Deliverables**
- Coarse draw-time budget model (cost per command / cost per pixel-write).
- VDP1 busy/draw-end timing affects: IRQ timing + frame-change gating behavior from G1.5.

**Exit Criteria:** Timing-sensitive test shows deterministic behavior differences when draw budget is exceeded vs met.

---

## H) Audio: SMPC-Gated 68EC000 First, Then SCSP (Gate H0)

### H1) SMPC-Gated Sound CPU Bring-Up (Integration Ordering is Explicit)

**Why:** Sega guidance states stopping/starting the Sound CPU must be done via SMPC Sound OFF/ON commands; other methods are prohibited.

**Deliverables**
- Implement `SNDON`/`SNDOFF` integrated path: SNDON observed → 68k reset released → 68k begins execution at reset vector.
- Unit tests for 68k core in isolation are allowed, but the integrated boot criterion is SMPC-gated.

**Exit Criteria:** Trace shows `SNDON` → reset release → 68k executes N instructions deterministically.

---

### H2) 68k Execution MVP

**Deliverables**
- 68k core executes enough to run a minimal init routine.
- Required memory windows routed + traced.

**Exit Criteria:** Deterministic 68k PC checkpoints for a known minimal program.

---

### H3) SCSP Baseline (PCM Path First)

**Deliverables**
- SCSP register block + sound RAM mapping.
- Basic PCM/mix output to host audio.
- Deterministic audio hash over fixed sample windows.

**Exit Criteria:** Audio micro-test yields stable audio hashes and trace-visible timer/IRQ events.

---

## I) CD Subsystem Split: Boot vs Streaming Cadence (Gate I0)

### I1) Boot-From-Disc Milestone

**Deliverables**
- Disc image ingestion + basic CD block command protocol sufficient to boot.
- Deterministic sector reads (scheduler-driven, trace-visible).

**Exit Criteria:** Boots at least one selected v1.0 title from disc to an executable checkpoint.

---

### I2) Streaming Cadence / Latency Correctness Milestone

**Why:** Sega bulletins specify a periodic response update cadence tied to CD-drive communication timing (e.g., 13.3 ms standard-speed, 6.7 ms double-speed). This must be modeled as a timed subsystem under the scheduler.

**Deliverables**

*Cadence model*
- Periodic response timing modeled + trace-verified (scheduler-owned).
- Sector delivery cadence/buffering modeled (not "instant file reads").

*Status / error / polling-shape contract*
- Model timing shape of "not ready / busy / pause / buffer full"-style status transitions so polling loops behave realistically.
- Model command response timeouts as timed events: if the CD block does not respond within the designated time after a command, a transmission time-out error occurs.
- Define a deterministic error-code contract for failed reads/commands (including timeout).

*Retry behavior (v1.0 stance must be explicit)*
- Provide a minimal, deterministic retry surface consistent with Saturn developer guidance.
- v1.0 does not need to simulate physical media defects, but if errors/timeouts are produced (real or injected via test harness), their timing and status transitions must be deterministic and spec-shaped.

*Tests*
- "Polling loop" test: issue read, poll status at realistic intervals, verify transitions + timeouts occur with modeled timing.
- If buffering modeled: add a "buffer-full → pause/blocked" behavior test.

**Exit Criteria:** CD cadence + polling tests pass with deterministic timing markers and stable trace hash.

---

## J) Compatibility Ladder + Performance Gates (Gate J0)

### J-pre) Title Selection + Requirements Matrices (Must Happen Before Finalizing G/H/I Scope)

**Why:** Until titles are named, G/H/I scope is not concrete.

**Deliverables**
- Select `[TITLE_A..E]` (one per bucket below).
- For each title, check in a 1-page requirements matrix:
  - Required VDP1 features + composition expectations (G1.5/G3)
  - Required VDP2 features + frame-change mode expectations
  - CD behavior expectations (data streaming vs CD-DA streaming vs minimal disc usage)
  - Audio expectations (68k/SCSP usage patterns)
  - Checkpoint definition for "gameplay"

**Exit Criteria:** Matrices exist in repo and are referenced by the relevant G/H/I subsections.

---

### J1) v1.0 Buckets + Chosen Titles

| Bucket | Rationale | Selected Title |
|---|---|---|
| 2D fighter | VDP1 sprite throughput + VDP2 planes/priorities + tight input | [TITLE_A] |
| 3D polygon-heavy | VDP1 command list + draw-end pacing | [TITLE_B] |
| Racing / frame-pacing sensitive | VDP1 timing budget realism + scheduler stability | [TITLE_C] |
| CD streaming/latency sensitive | I2 load-bearing; specify whether CD-DA is continuous | [TITLE_D] |
| Audio/system-integration heavy | SMPC↔68k↔SCSP interplay | [TITLE_E] |

**Recommended initial candidates (provisional — validated and adjusted by J-pre matrices):**
- 2D fighter → Street Fighter Alpha 2
- 3D polygon → Virtua Fighter 2
- Racing → Sega Rally Championship
- CD streaming/latency → Resident Evil
- Audio/system-heavy → NiGHTS into Dreams

> These candidates are all legitimate Saturn titles with strong community documentation. However, what makes a title truly qualify for its bucket — particularly the CD streaming/latency bucket — is not always obvious from the title alone. Continuous CD-DA usage, polling loop patterns, buffer assumptions, and sector timing expectations can only be confirmed by reading the actual code paths the title exercises. The J-pre matrix process is the mechanism for validating these claims. Treat the list above as a starting point, not a commitment, until matrices confirm the bucket assignment for each title.

---

### J2) Compatibility Ladder (Objective Convergence)

Boot BIOS → boot a homebrew test → boot each of the 5 v1.0 titles to gameplay.

For each rung/title, maintain:
- Trace hash at checkpoints
- Frame hash window(s)
- Audio hash window(s)

**Exit Criteria:** Compat suite run produces stable, comparable results across commits.

---

### J3) Performance Gates (Avoid "Interpreter Forever")

**JIT start trigger (hard):** Start JIT/dynarec only when:
- Deterministic replay is stable,
- 5 v1.0 titles reach gameplay, and
- Trace-diff workflow localizes regressions reliably.

**Rule:** Parallel compute allowed; serial commit through scheduler/arbiter remains the determinism backbone.

---

## Appendix: Reference Validation Strategy

*How to verify accuracy without commercial ROMs or real hardware for most of development.*

| Verification method | What it covers | Limitation |
|---|---|---|
| SH-2 spec (Hitachi manuals) | CPU instruction correctness by definition | Doesn't cover hardware interactions |
| Mednafen comparative trace | Strong confidence for most behaviors | Mednafen is a reference, not ground truth |
| Community hardware analysis | Specific timing/register behaviors | Coverage is uneven |
| Homebrew test ROMs (Jo Engine etc.) | Real Saturn binary behavior | May not stress every subsystem |
| Synthetic test programs (sh-elf-gcc) | Targeted, controlled CPU/bus tests | You write them; they test what you think to test |
| Real hardware capture | Ground truth for frame/audio hashes | Required for final J2 golden fixtures |

**Practical split:** Let Codex own synthetic tests, trace diffing, bug fixes with clear specs, and infrastructure. Reserve human judgment for reference capture, subjective correctness calls, and title selection.
