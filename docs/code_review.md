# Code Review Snapshot

Date: 2026-02-17

## Review summary

1. **Deterministic bus arbitration and commit safety remain stable.**
   - Existing commit-horizon/progress-watermark and deterministic ordering tests continue to pass.
2. **SCU synthetic-source MMIO coverage is now contention-aware and timing-aware.**
   - Mixed-CPU contention on synthetic-source writes is covered and deterministic.
   - Subword lane-mask behavior is covered.
   - MMIO commit `stall` fields for synthetic-source writes are now regression-checked across repeated runs.
   - Trace JSONL commit ordering for synthetic-source MMIO writes remains asserted.
3. **BIOS deterministic trace coverage now includes commit timing checkpoints.**
   - Fixed BIOS fixture comparisons remain stable.
   - Master/slave state checkpoints are covered.
   - Deterministic commit timing checkpoints (`t_start`, `t_end`, `stall`) for fixed IFETCH slice are covered.
4. **SH-2 branch/delay-slot coverage expanded for memory-op slots.**
   - BRA/RTS with MOV.W delay-slot memory operations are now covered with deterministic branch-after-slot behavior.
   - Branch-in-delay-slot first-branch-wins policy remains documented and tested.

## Risks and follow-ups

- SCU source wiring is still synthetic in this slice; full hardware source modeling remains TODO.
- SH-2 remains a vertical-slice subset (no full timing/ISA/exception model).
- More mixed-size and cross-policy branch edge cases remain under-tested.

## TODO tracking

Backlog and next tasks are maintained in `docs/todo.md`.
