# Code Review Snapshot

Date: 2026-02-17 (challenge session update)

## Review summary

### 2026-02-19 Group 4 (addressing-mode breadth) implementation review
- Added/validated SH-2 addressing-mode coverage for real firmware-style data paths:
  1. PC-relative forms: `MOV.W @(disp,PC),Rn`, `MOV.L @(disp,PC),Rn`, and `MOVA @(disp,PC),R0`.
  2. Indexed `@(R0,Rn)` load/store families for `MOV.B/W/L`.
  3. Expanded `@(disp,GBR)` scaling/sign-extension matrix assertions across byte/word/long variants.
- Added deterministic robustness checks for this batch:
  1. Unaligned indexed long access behavior is now pinned by deterministic modeled-RAM behavior + repeated-run trace parity assertions.
  2. Decode-collision audit vectors for new addressing encodings remain exclusive under decode pattern matching.
- Reviewed bus sequencing impact: all new memory forms continue through the existing pending memory-op and arbiter commit pipeline.

### 2026-02-19 Group 3 (decode hardening) implementation review
- Added a shared decode helper module with:
  1. Centralized nibble/immediate/displacement extraction helpers.
  2. Canonical decode family pattern table for currently modeled SH-2 families.
  3. Deterministic `decode_match_count` auditing path for overlap/exclusivity checks.
- Added decode-focused regressions for:
  1. Adjacent-encoding corpus around control-flow opcodes (`JSR`/`JMP`/`BSR` boundaries).
  2. Full 65536-opcode exclusivity assertion over decode patterns.
  3. Decode-family corpus coverage (each pattern family has at least one vector).
  4. Unknown opcode behavior pinned to deterministic `ILLEGAL_OP` (no implicit NOP fallback).
- Refactoring note: extracted branch/system register field reads in SH-2 execute path to shared decode field helpers to reduce nibble-extraction drift risk.

### 2026-02-19 Group 2 (control-flow blockers) implementation review
- Reviewed SH-2 control-flow behavior against Group 2 roadmap goals with emphasis on decode correctness and delayed-branch determinism.
- Added/validated coverage for:
  1. `JSR @Rn` non-R0 decode path (`@R3`) with PR link and delay-slot behavior.
  2. Boundary displacement semantics for `BSR`, `BT/BF`, and `BT/S`/`BF/S` forms.
  3. Branch delay-slot matrix coverage across `{BRA, BSR, JMP, JSR, BT/S, BF/S}` with both ALU-slot and memory-slot variants.
- Determinism/code-quality review outcome:
  1. Repeated-run branch matrix traces are byte-identical.
  2. Control-flow state (`PC/PR/register side effects`) matches across repeated executions.
  3. Delay-slot memory paths remain routed through existing bus-commit sequencing.

### 2026-02-19 Group 1 (exception handler prologue/epilogue) implementation review
- Reviewed SH-2 decode/execute changes for `STS.L PR,@-Rn`, `LDS.L @Rm+,PR`, `LDC Rm,SR`, and `STC SR,Rn`.
- Verified each new instruction path keeps deterministic semantics:
  1. Stack-address updates are register-derived only (no wall-clock/random inputs).
  2. Memory forms use existing pending-bus operation pipeline to preserve commit ordering.
  3. SR/PR register state transitions are explicit and trace-visible through existing snapshots.
- Added a focused regression that exercises all four forms together and validates SR readback, PR roundtrip, and stack pointer invariants.

### 2026-02-19 TODO/PR documentation reconciliation
- Reviewed the latest non-merge PR commit span (`5c33242`..`a6a13a0`) against the tracked TODO backlog to ensure completed work is captured in project docs.
- Confirmed recently landed ISA/control-flow fixes are now explicitly tracked in `docs/todo.md`:
  1. `BSR` implementation with deterministic delay-slot + PR link semantics.
  2. `JMP @Rm` implementation with deterministic delayed-branch behavior.
  3. Stack-based `RTE` delayed-target restore hardening.
  4. `JSR @Rm` register decode correction.
  5. `MOV.B R0,@(disp,Rn)` displacement decode correction (byte addressing semantics).
- Backlog hygiene update: removed `BSR`/`JMP @Rm` from next-batch candidates now that they are completed.
- Added a commit-to-TODO audit block in `docs/todo.md` so each recent behavior-changing PR has explicit backlog coverage linkage.
- Current highest-value ISA follow-ups remain: indexed `@(R0,Rn)`/`@(disp,GBR)` addressing, sign/zero-extension ops, and additional ALU/shift/mul/div forms.

### 2026-02-18 priority review (repeat hardening session)
- Conducted a focused review over VDP1 command/completion MMIO scaffolding, arbiter fault payload determinism, scripted store-buffer boundedness, and ST/MT trace-prefix parity checks.
- Prioritized risk classes for this session:
  1. VDP1 command-status behavior drift (lane/read-only semantics) under future register expansion.
  2. Producer/arbiter contract regressions that could surface as false monotonic faults or unstable detail payloads.
  3. Long-run scripted stress risk (store-buffer growth or prefix-drift under MT contention).
- Session closeout notes:
  1. Added deterministic tests for VDP1 command-status read-only + byte-lane completion pulse behavior.
  2. Added CPU1-owner stress visibility and additional commit-prefix parity checks.
  3. Added explicit boundedness checks for store-buffer stress and retained fail-loud arbiter detail assertions.
- Remaining medium-priority follow-ups:
  1. Introduce non-CPU producer provenance for VDP1 events when a fuller renderer pipeline exists.
  2. Expand synthetic exception coverage into nested return-order semantics once stack-accurate RTE is modeled.

### 2026-02-18 completion review (new 16-task batch closeout)
- Completed the 16-task batch with focused deterministic coverage additions around VDP1 command-status lane semantics, command-completion/SCU-ack behavior, alternating ownership stress parity (CPU0-owner and CPU1-owner fixtures), enqueue-contract regression hardening, and counter-wrap policy assertions.
- Added/extended deterministic trace checks for command-completion metadata (`src`/`owner`/`tag`) and first-32 commit-prefix stability under repeated multithread runs.
- Added targeted robustness tests for store-buffer bounded retirement stress and encoded INVALID_BUS_OP detail-class payload checks.
- Residual risks after this closeout pass:
  1. VDP1 command/completion behavior remains intentionally synthetic and not cycle/hardware-accurate.
  2. TinyCache mismatch parity in true producer-threaded execution still relies on existing deterministic coordinator behavior and should be revisited when broader cache producer paths are added.

### 2026-02-18 focused follow-up review (VDP1 command/completion vertical slice)
- Performed a focused review on VDP1 source-event scaffolding, trace regression parity, and deterministic single-thread/multithread stress behavior.
- Completed the remaining VDP1 TODOs by introducing a minimal command/completion-producing path and pinning status/IST timing tuples across ST/MT stress fixtures.
- Confirmed deterministic invariants still hold in the reviewed paths:
  1. Command submission and completion are explicit MMIO transitions (no wall-clock dependence).
  2. Source-event counter/IRQ/pending transitions remain deterministic and trace-visible.
  3. ST/MT scripted stress traces for the new VDP1 path are byte-identical in repeated runs.
- Residual risks after this pass:
  1. VDP1 command/completion path is still synthetic and not mapped to real draw-list execution semantics.
  2. Event-counter wrap/saturation policy is currently implementation-defined and should be explicitly documented/tested next.

### 2026-02-18 focused follow-up review (multithread bounded waiting + VDP1 source event path)
- Performed another focused pass over scripted multithread coordination, VDP1/SCU interrupt scaffolding, bus validation details, and deterministic trace regressions.
- Completed previously open high-value follow-ups:
  1. Replaced pure yield-based hot-loop behavior in `run_scripted_pair_multithread` with deterministic bounded waiting via an explicit signal hub (`condition_variable` wake-on-progress model).
  2. Expanded VDP1->SCU handoff from bridge-only toggling to a first source-driven event trigger path with deterministic event counter/status visibility.
  3. Added repeated-run trace-level assertions for VDP1 handoff commits, pinning `src`/`owner`/`tag` fields and stable timing-line tuples.
- Current top residual risks after this pass:
  1. Scripted multithread coordination still relies on host thread scheduling for wake timing (behavior remains deterministic under current arbitration rules/tests, but throughput profile is host-dependent).
  2. VDP1 interrupt-source model remains synthetic (event trigger scaffold), not yet mapped to real rendering/command completion sources.

### 2026-02-18 focused review (post-SMPC/VDP1 scaffold)
- Performed a focused pass over bus arbitration, SH-2 execute/load paths, device MMIO scaffolds, trace regressions, and TODO hygiene.
- Confirmed deterministic invariants still hold under current test matrix (single-thread and multithread trace parity loops remain stable in CI loop).
- Identified highest-value follow-up risks for immediate backlog:
  1. SH-2 mixed-width overwrite coverage is partially converged: former TODO-guard cases now assert target-side semantics for focused BRA/RTS negative-immediate scenarios, while broader RTS matrix entries still encode modeled behavior.
  2. `BusArbiter` does not currently validate unsupported bus op sizes in a centralized way (size assumptions are implicit in call sites).
  3. `run_scripted_pair_multithread` remains yield-based busy-wait coordination (correct but higher host overhead and harder to reason about under stress).
  4. VDP1->SCU handoff remains a synthetic bridge register scaffold rather than source-accurate interrupt production.

### Latest rolling batch updates
- Converted SH-2 mixed-width BRA/RTS overwrite TODO guards into architectural target-side overwrite assertions (including explicit PR setup for RTS paths).
- Performed another focused code-review pass after SMPC + VDP1/SCU updates; current residual risks: SH-2 mixed-width BRA/RTS target-side overwrite coverage still has explicit TODO-backed modeled-value guards, and broader non-synthetic VDP1 interrupt-source modeling remains pending.
- Added first VDP1->SCU interrupt handoff scaffold behavior: deterministic bridge register toggling a SCU pending source bit with focused set/clear/masked-visibility assertions.
- Implemented the first SMPC command write/read vertical slice beyond status-ready defaults: command register latching, deterministic result register encoding, and ready-bit stability checks.
- Addressed external code-review critical correctness findings: removed erroneous PR updates on SH-2 loads to R15 (`ReadByte`/`ReadWord`/`ReadLong`) and switched committed memory + tiny-cache multi-byte read/write semantics to big-endian ordering.
- Added focused regression coverage for big-endian memory/cache byte layout and store-buffer overflow retention (no silent eviction beyond 16 queued stores).
- Expanded BIOS trace fixture flow to append one deterministic DMA-routed MMIO write/read pair via `commit_dma`, and added fixture assertions that both DMA MMIO write/read commits are present.
- Added focused commit-horizon fairness regression where CPU and DMA producers contend on the same MMIO address; test now pins DMA-first priority with immediate CPU follow-up visibility.
- Completed a full-project review pass (bus/core/cpu/devices/tests/docs) and confirmed the current TODO ordering remains focused on highest-value next work: DMA trace/provenance completion and DMA-vs-CPU fairness before broader SMPC/VDP vertical slices.
- Added deterministic first DMA MMIO commit timing/value tuple assertions and introduced explicit trace provenance fields (`owner`, `tag`) for future DMA/SCU arbitration analysis.
- Converted the DMA trace-regression TODO scaffold into an executable deterministic DMA bus-op path test using `BusArbiter::commit_dma` write+readback runs.
- Aligned SH-2 semantics and RTS regressions to stop mirroring PR from SP writes; RTS target tests now set PR explicitly where needed.
- Began the new TODO batch with deterministic SCU overlap coverage for three-lane mixed-size writes plus alternating clear masks and staggered-req-time IMS/set/clear interleaving.
- Added deterministic SCU write-log lane-specific per-CPU address histogram stability checks under mixed-size bursts.
- Expanded SH-2 delay-slot overwrite matrix with BRA/RTS target-side MOV+ADD+ADD-before-store variants.
- Completed the remaining in-progress batch items: six-intermediate SH-2 overwrite flow, eleven-cycle commit-horizon drain, eight queued MMIO-read pinning, four progress reversals, multithread MMIO_READ/MMIO_WRITE/BARRIER triplet order checks, BIOS per-CPU MMIO/BARRIER timing parity, dual-demo per-CPU IFETCH + second-occurrence READ/MMIO parity, first-28 commit-prefix parity, and a DMA-path TODO scaffold guard.

- Added SCU overlap regressions for mixed-size three-lane writes, interleaved IST/source clear idempotence, alternating IMS byte-mask bursts with concurrent set/clear, and per-CPU address+value write-log histogram stability.
- Expanded commit-horizon regressions to cover ten-cycle mixed RAM/MMIO drains, seven queued MMIO-read deterministic pinning across runs, and three alternating progress reversals before convergence.
- Expanded SH-2 mixed-width delay-slot overwrite matrix with BRA/RTS target-side MOV+ADD-before-store variants and a five-intermediate non-memory overwrite flow.
- Completed the previously in-progress trace-regression items for this batch (per-CPU READ/MMIO timing tuples, selected BARRIER timing tuples, BIOS per-CPU IFETCH timing tuples, and first-24 prefix parity).

- Added SCU overlap regressions for non-adjacent lane byte writes, repeated source-clear idempotence, alternating halfword IMS masking, and write-log address histogram stability.
- Expanded commit-horizon regressions to cover nine-cycle mixed RAM/MMIO drains, six queued MMIO-read pinned values, and alternating reversal convergence on both CPUs.
- Expanded SH-2 mixed-width delay-slot overwrite matrix with BRA/RTS target-side register-copy variants and a four-intermediate non-memory overwrite path.
- Expanded trace regression with per-CPU IFETCH src parity, IFETCH/MMIO_READ timing tuple parity, BIOS per-CPU BARRIER parity, and first-20 commit-prefix stability.

- Added SCU overlap regressions for opposite-lane halfword/byte interactions, three-batch alternating contention, replayed IST-clear agreement, and per-CPU write-log value histogram stability.
- Expanded commit-horizon regressions to cover eight-cycle drains, five queued MMIO-read pinned values, and double-reversal progress convergence.
- Expanded SH-2 mixed-width delay-slot overwrite matrix with dual target arithmetic variants and a three-intermediate non-memory overwrite path.
- Expanded trace regression with per-CPU READ/BARRIER parity, selected MMIO_WRITE timing tuple parity, first-16 commit-prefix stability, and BIOS per-CPU MMIO kind distribution stability.

- Added SCU overlap masked-lane, idempotence, alternating-window, and write-log per-CPU stability regressions.
- Added commit-horizon seven-cycle and pinned multi-read value regressions plus midpoint progress-reversal convergence checks.
- Expanded SH-2 both-negative overwrite coverage with follow-up target arithmetic and longer non-memory interposed flows.
- Expanded trace regression coverage for per-CPU src parity, timing tuple parity, BIOS per-CPU src distributions, and first MMIO_READ ordering.

### Latest 16-task follow-up updates
- Added SCU overlap lane-accuracy, idempotence, alternating-burst consistency, and write-log delta stability checks.
- Expanded commit-horizon coverage to include six-cycle drains, pinned queued-MMIO-read value checks, and alternating asymmetric progress updates on both CPUs.
- Expanded dual-demo and BIOS trace assertions for src-distribution parity and exact mixed-kind commit prefix stability.
- Expanded SH-2 mixed-width matrix with both-negative immediate overwrite scenarios and intermediate non-memory instruction overwrite flow.

### Latest review updates
- Tightened one previously permissive commit-horizon assertion to bind expected MMIO-read value to the MMIO-read op itself (by address), reducing false-pass risk.
- Documented that some RTS mixed-width overwrite checks currently encode modeled behavior; MMIO-vs-RAM same-address overwrite remains TODO in the current SH-2 subset and now has a focused determinism guard test.
- Added deterministic SCU overlap, commit-horizon, and trace-order checks aligned with the current TODO batch.

### Expanded 16-task batch coverage
- Added deterministic overlap set/clear two-batch SCU assertions with round-robin rotation checks.
- Added four-cycle mixed RAM/MMIO commit-horizon drain and queue-order preservation checks.
- Added mixed-width same-address SH-2 delay-slot overwrite tests for BRA/RTS paths.
- Added single-thread vs multithread dual-demo per-kind count parity checks across all current commit kinds.

1. **Deterministic bus arbitration and commit safety remain stable.**
   - Existing commit-horizon/progress-watermark and deterministic ordering tests continue to pass, now including long mixed RAM/MMIO queue drains across three horizon-advance cycles with order-preservation checks.
2. **SCU synthetic-source MMIO coverage now spans mixed-size contention, lanes, overlapping clear masks, overlapping set/clear batches, trace order, and stall stability.**
   - Mixed-CPU and mixed-size contention paths are covered with deterministic expectations, including same-batch overlapping set/clear and overlapping clear-mask scenarios.
   - Subword lane-mask behavior remains covered.
   - MMIO commit `stall` fields are regression-checked across repeated runs.
   - Trace JSONL ordering for synthetic-source MMIO commits remains asserted.
3. **BIOS deterministic trace coverage now includes event-count stability and timing checkpoints.**
   - Fixture comparisons remain stable.
   - Master/slave checkpoint progressions remain covered.
   - Selected IFETCH commit timing checkpoints and MMIO/READ/WRITE/BARRIER count stability are asserted.
   - BIOS fixture cache-hit true/false commit-count stability is now explicitly asserted across repeated runs.
   - DMA-tagged commit count checks are pinned to deterministic zero until DMA-tagged paths are introduced.
4. **SH-2 branch/delay-slot coverage includes memory-op slot interactions.**
   - BRA/RTS with MOV.W and MOV.L delay-slot memory read/write operations are covered and deterministic, including same-address delay-slot-store plus target-store overwrite scenarios.
   - Branch-in-delay-slot first-branch-wins policy remains documented and tested.

## Risks and follow-ups

- SCU source wiring is still synthetic in this slice; full hardware source modeling remains TODO.
- DMA-tagged bus-op commits are now covered by a focused deterministic write/read trace regression; BIOS fixture DMA flow remains TODO.
- SH-2 remains a vertical-slice subset (no full timing/ISA/exception model).

## TODO tracking

Backlog and next tasks are maintained in `docs/todo.md`.
