# libbusarb Integration Notes (Ymir-focused)

`libbusarb` is a small deterministic stall-oracle/arbitration seam. It is **not** a full bus simulator.

## What it provides

- Public, standalone headers under `include/busarb/`.
- Deterministic query/commit API:
  - `query_wait(req)` (non-mutating)
  - `commit_grant(req, tick_start, had_tie=false)` (mutating)
- Deterministic same-tick winner selection via fixed priority (`DMA > SH2_A > SH2_B`) using `pick_winner(...)`.

## Callback contract (`TimingCallbacks::access_cycles`)

## API versioning

- Public header exposes semantic API version constants:
  - `busarb::kApiVersionMajor`
  - `busarb::kApiVersionMinor`
  - `busarb::kApiVersionPatch`
- Current value: **1.1.0**.

- Inputs are passed through unchanged from `BusRequest`:
  - `addr`
  - `is_write`
  - `size_bytes`
- Return value is the service duration in caller-defined tick units.
- Determinism requirement: same input tuple must return the same duration.
- If callback returns `0`, `libbusarb` clamps it to `1` tick to preserve forward progress.

## API semantics (explicit)

### `BusRequest::now_tick`

- Opaque monotonic caller-owned tick value.
- Units are defined by the caller/integration layer.
- Repeated `query_wait(...)` calls at the same tick are valid and must be stable until a commit changes state.

### `BusWaitResult`

- `wait_cycles` is **stall-only** delay until a request may begin.
- `wait_cycles` is a **minimum delay**, not a prediction under future contention.
- `should_wait == false` implies `wait_cycles == 0`.

### `query_wait(...)`

- Does not mutate arbiter state.
- Designed to be call-order independent for same-tick contenders.

### `commit_grant(...)`

- This is the only mutating API path.
- Prior `query_wait(...)` call is not required.
- Duplicate commit calls model duplicate grants and will advance state again.
- `had_tie=true` applies tie-turnaround penalty using configured `ArbiterConfig::tie_turnaround`.
- `had_tie=false` preserves non-tie commit behavior.

## Minimal Ymir adapter pattern

1. Build contender request set for `now_tick`.
2. Call `query_wait(...)` for each contender.
3. If multiple contenders can proceed at that tick, call `pick_winner(...)`.
4. Commit exactly one winner with `commit_grant(...)`.
5. Re-query on next scheduling decision.

This avoids caller-order artifacts if Ymir happens to query contenders in a fixed order.

## Current limitations

- No MA/IF stage-aware contention model (deferred to Track B).
- No full Saturnis memory/router integration by design.
- Fixed-priority tie-break policy may starve lower-priority masters under sustained DMA load.
