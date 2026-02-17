# Code Review Snapshot

Date: 2026-02-17

## Review summary

1. **Deterministic arbitration and commit-horizon behavior remain sound.**
   - `BusArbiter` ordering and progress-watermark gating paths are covered by focused kernel tests.
2. **MMIO model now includes basic SCU interrupt status/mask interactions.**
   - SCU IMS masking remains deterministic.
   - SCU IST pending-bit visibility now interacts with IMS mask bits.
   - Added deterministic clear semantics through an explicit SCU interrupt-clear register path.
3. **SH-2 execution slice expanded with additional deterministic data-memory forms.**
   - Added MOV.W data-memory read/write bus paths with focused regression coverage (including sign-extension behavior).

## Risks and follow-ups

- SCU interrupt model is still intentionally reduced (software-driven pending bits only; no device-side event generation yet).
- Device behavior is still register-fragment based; subsystem-level interactions are mostly unmodeled.
- SH-2 coverage is still vertical-slice scope, not full ISA behavior (many addressing/transfer forms remain).

## TODO tracking

Backlog and execution order are maintained in `docs/todo.md`.
