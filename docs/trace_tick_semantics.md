# Trace Tick Semantics Contract

This document defines the timing semantics used by Phase 1 replay.

## Field semantics

- `tick_first_attempt`: Tick at which Ymir began the first attempt for the access.
- `tick_complete`: Tick at which Ymir completed the successful access.
- `service_cycles`: Service duration for one granted attempt (`AccessCycles`).
- `retries`: Number of blocked attempts before success.

## Endpoint convention

Replay treats `tick_complete` as an **inclusive endpoint** for elapsed timing.

- `ymir_elapsed = tick_complete - tick_first_attempt + 1` when `tick_complete >= tick_first_attempt`
- fallback proxy when ticks are inconsistent:
  - `ymir_elapsed = service_cycles + retries * service_cycles`

This inclusive convention fixes the observed `delta_total = delta_wait + 1` artifact caused by mixing exclusive and inclusive formulas.

## Derived Ymir values

- `ymir_wait = max(0, ymir_elapsed - service_cycles)`

## Replay-side values

For each record in comparative replay:

- `arbiter_predicted_wait = local contention estimate` using only immediate neighboring access context (same-address and SH-2 tie-turnaround heuristics), intentionally decoupled from long-run arbiter backlog
- `arbiter_predicted_service = max(1, ymir_access_cycles(...))`
- `arbiter_predicted_total = arbiter_predicted_wait + arbiter_predicted_service`

## Drift and normalized metrics

Two views are reported:

1. **Cumulative drift**
   - `cumulative_drift_wait = arbiter_start - tick_first_attempt`
   - `cumulative_drift_total = arbiter_commit_end - (tick_complete + 1)`

2. **Normalized per-access delta**
   - `normalized_delta_wait = arbiter_predicted_wait - ymir_wait`
   - `normalized_delta_total = arbiter_predicted_total - ymir_elapsed`

Normalized deltas are intended for local access comparison; cumulative drift tracks timeline divergence.
