# Code Review Snapshot

Date: 2026-02-16

## Full-project review summary

### What is currently solid

1. **Deterministic bus arbitration and commit safety**
   - `BusArbiter` ordering is deterministic (start-time/priority/RR/final stable tie-break).
   - Progress-watermark horizon gating is covered in tests, including pending-queue retention behavior.
2. **Trace determinism guardrails are active**
   - Single-thread and multithread dual-demo traces are regression-checked for stability.
3. **DeviceHub has moved beyond pure stubs**
   - Generic deterministic MMIO latching/readback exists.
   - Initial explicit register semantics now cover display status (`0x05F00010`), SCU DMA0 enable/mode (`0x05FE0010`/`0x05FE0014`), SCU IMS/IST/ICR (`0x05FE00A0`/`0x05FE00A4`/`0x05FE00A8`) with deterministic masks/read-only behavior.
4. **Core test loop remains healthy**
   - Kernel tests and trace-regression tests pass under the required build/test loop.

### Key gaps found in this review

1. **Device model breadth is still narrow**
   - A focused SCU starter set is now modeled, but broader SMPC/SCU/VDP/SCSP register maps remain unspecified.
2. **SH-2 interpreter data-memory path is still incomplete**
   - Current SH-2 path is mostly IFETCH-focused; deterministic data load/store execution flow remains to be integrated.
3. **BIOS-mode behavior remains bring-up quality**
   - BIOS execution path is functional for experimentation but still partial/non-cycle-accurate by design.

## Updated goals / TODO (prioritized)

1. **Expand explicit device semantics with deterministic register tests**
   - Add small, high-value modeled registers per block (SCU first, then SMPC/VDP/SCSP).
   - For each register: define reset value, read/write mask, and side-effect policy.
   - Add focused tests for each new register behavior and masking rule.

2. **Integrate SH-2 data-memory execution path behind trace regression**
   - Introduce deterministic SH-2 load/store bus op emission where not cache-hit/forwarded.
   - Preserve local store-buffer forwarding + cache behavior and ensure trace stability.
   - Add focused mixed IFETCH/data-memory tests plus regression traces.

3. **Tighten BIOS-mode bring-up correctness checks**
   - Add deterministic smoke tests around reset/PC progression and stable trace shapes.
   - Keep accuracy expectations explicit (research vertical-slice scope).

4. **Keep determinism hardening continuous**
   - Maintain single-vs-multithread trace parity checks as features are added.
   - Add targeted tests whenever arbitration, progress-horizon, or device semantics change.

## Recommended next implementation order

1. Continue SCU register set expansion beyond the starter set (deterministic masks + status bits + tests).
2. SMPC minimal command/status register semantics with strict deterministic behavior.
3. SH-2 data-memory execution path integration with regression traces.
4. VDP/SCSP register scaffolding only after prior items are stable.
