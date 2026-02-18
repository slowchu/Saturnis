# Code Review Snapshot

Date: 2026-02-17 (challenge session update)

## Review summary

### Latest rolling batch updates
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
