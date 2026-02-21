# libbusarb Integration Notes (Ymir-focused)

`libbusarb` is a small deterministic stall-oracle library. It is **not** a full bus simulator.

## What it provides

- Public, standalone headers under `include/busarb/`.
- Deterministic query/commit API:
  - `query_wait(req)` (non-mutating)
  - `commit_grant(req, tick_start)` (mutating)
- Deterministic same-tick winner selection via fixed priority (`DMA > SH2_A > SH2_B`) using `pick_winner(...)`.

## Callback contract

Timing comes from caller-provided callback (`TimingCallbacks::access_cycles`):

- input: `addr`, `is_write`, `size_bytes`
- output: service duration cycles

This keeps timing authority in caller code (Ymir), not inside `libbusarb`.

## Ymir seam mapping

- In wait path (`IsBusWait`-like logic), build `BusRequest` with current monotonic tick and call `query_wait(req)`.
- For same-tick contenders, gather requests and call `pick_winner(...)`.
- When access proceeds, call `commit_grant(req, tick_start)` with chosen winner and start tick.

## Current limitations

- No MA/IF stage-aware contention model.
- No full Saturnis memory/router integration by design.
- Fixed-priority tie-break policy for P0/P1 integration simplicity.
