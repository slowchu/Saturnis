# Code Review Snapshot

Date: 2026-02-17

## Review summary

1. **Deterministic bus arbitration and commit safety remain stable.**
   - Existing commit-horizon/progress-watermark and deterministic ordering tests continue to pass, including new mixed RAM/MMIO pending-queue progress-cycling scenarios.
2. **SCU synthetic-source MMIO coverage now spans mixed-size contention, lanes, overlapping clear masks, trace order, and stall stability.**
   - Mixed-CPU and mixed-size contention paths are covered with deterministic expectations, including overlapping same-batch source-clear masks.
   - Subword lane-mask behavior remains covered.
   - MMIO commit `stall` fields are regression-checked across repeated runs.
   - Trace JSONL ordering for synthetic-source MMIO commits remains asserted.
3. **BIOS deterministic trace coverage now includes event-count stability and timing checkpoints.**
   - Fixture comparisons remain stable.
   - Master/slave checkpoint progressions remain covered.
   - Selected IFETCH commit timing checkpoints and MMIO/READ/WRITE/BARRIER count stability are asserted.
   - DMA-tagged commit count checks are pinned to deterministic zero until DMA-tagged paths are introduced.
4. **SH-2 branch/delay-slot coverage includes memory-op slot interactions.**
   - BRA/RTS with MOV.W and MOV.L delay-slot memory read/write operations are covered and deterministic.
   - Branch-in-delay-slot first-branch-wins policy remains documented and tested.

## Risks and follow-ups

- SCU source wiring is still synthetic in this slice; full hardware source modeling remains TODO.
- DMA-tagged commit paths are not modeled yet; count assertions currently enforce zero tagged events for stability.
- SH-2 remains a vertical-slice subset (no full timing/ISA/exception model).

## TODO tracking

Backlog and next tasks are maintained in `docs/todo.md`.
