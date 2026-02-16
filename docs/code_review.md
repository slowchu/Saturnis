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

## Remaining high-value work

1. **Expand device model semantics.**
   - `DeviceHub` still has stubbed reads and MMIO-write logging only.
2. **Complete SH-2 data-memory execution path.**
   - Architecture notes still call out partial data-memory integration.

## Recommended next implementation order

1. Expand device read/write semantics incrementally with deterministic tests.
2. Continue SH-2 data-memory integration behind regression traces.
