# Code Review Snapshot

Date: 2026-02-16

## Full-project review summary

1. **`BusArbiter` compile regression fixed.**
   - Removed duplicated `update_progress`/`commit_horizon` declarations.
   - Restored `commit_batch` function structure so arbitration/commit flow compiles and runs.
2. **`commit_pending` is now implemented.**
   - It commits currently committable operations and keeps only uncommitted ops in the caller-provided pending queue.
3. **Progress-horizon terminology tightened.**
   - Commit-horizon tests/assert messages now consistently refer to CPU **progress watermarks**.
   - Added explicit arbiter comment clarifying that horizon gating opens only after both progress watermarks are published.

4. **DeviceHub semantics expanded incrementally.**
   - MMIO writes now latch into deterministic register storage with byte/halfword lane updates.
   - MMIO reads now return latched values (with deterministic defaults for unwritten status registers) and have focused kernel coverage.

1. **Deterministic bus arbitration and commit safety**
   - `BusArbiter` ordering is deterministic (start-time/priority/RR/final stable tie-break).
   - Progress-watermark horizon gating is covered in tests, including pending-queue retention behavior.
2. **Trace determinism guardrails are active**
   - Single-thread and multithread dual-demo traces are regression-checked for stability.
3. **DeviceHub has moved beyond pure stubs**
   - Generic deterministic MMIO latching/readback exists.
   - Initial explicit register semantics now exist for display status (`0x05F00010`, read-only ready) and SCU IMS (`0x05FE00A0`, writable-mask behavior).
4. **Core test loop remains healthy**
   - Kernel tests and trace-regression tests pass under the required build/test loop.

1. **Expand device model semantics further.**
   - `DeviceHub` now supports deterministic register latching/readback but still lacks explicit SMPC/SCU/VDP/SCSP behavior.
2. **Complete SH-2 data-memory execution path.**
   - Architecture notes still call out partial data-memory integration.

## Recommended next implementation order

1. Continue device-specific semantics (SMPC/SCU/VDP/SCSP) with deterministic tests.
2. Continue SH-2 data-memory integration behind regression traces.
