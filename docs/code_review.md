# Code Review Snapshot

Date: 2026-02-16

## Recently resolved

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
5. **Initial device-specific register behavior added.**
   - Display status register is modeled as deterministic read-only ready (`0x05F00010`).
   - SCU IMS (`0x05FE00A0`) now enforces a deterministic writable-bit mask with focused tests.

## Remaining high-value work

1. **Expand device model semantics further.**
   - `DeviceHub` now has initial explicit register behavior, but broader SMPC/SCU/VDP/SCSP register maps are still incomplete.
2. **Complete SH-2 data-memory execution path.**
   - Architecture notes still call out partial data-memory integration.

## Recommended next implementation order

1. Continue adding explicit SMPC/SCU/VDP/SCSP register semantics with deterministic tests.
2. Continue SH-2 data-memory integration behind regression traces.
