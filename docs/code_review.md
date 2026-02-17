# Code Review Snapshot

Date: 2026-02-17

## Review summary

1. **Deterministic bus arbitration and commit safety remain stable.**
   - Existing commit-horizon/progress-watermark and deterministic ordering tests continue to pass.
2. **SCU synthetic-source MMIO coverage is now deeper and trace-aware.**
   - Source set/clear wiring is covered, including subword lane-mask behavior.
   - Device write-log metadata ordering and monotonic timestamps are covered.
   - Trace JSONL commit ordering for SCU synthetic-source MMIO writes is now asserted.
3. **BIOS deterministic trace coverage now checks both CPUs.**
   - Fixed BIOS fixture comparisons are repeated-run stable.
   - Master and slave CPU checkpoint progressions are both asserted.
4. **SH-2 delay-slot policy is now explicitly tested and documented.**
   - BRA/RTS delay-slot behavior remains covered.
   - Branch-in-delay-slot first-branch-wins policy now has focused regression coverage.

## Risks and follow-ups

- SCU source wiring is still synthetic in this slice; full hardware source modeling remains TODO.
- SH-2 remains a vertical-slice subset (no full timing/ISA/exception model).
- Commit timing-field stability assertions (`t_start`/`t_end`/`stall`) are still light in BIOS-focused tests.

## TODO tracking

Backlog and next tasks are maintained in `docs/todo.md`.
