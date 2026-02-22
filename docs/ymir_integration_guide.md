# Saturnis → Ymir Phase 1 Integration Guide

## 1) Phase 1 scope

`libbusarb` is used as an **offline reference/replay tool** in Phase 1.

- Runtime Ymir integration is deferred.
- Release builds do not need to run arbiter logic in hot paths.
- Goal: compare Ymir timing behavior against deterministic replay output, then calibrate Ymir tables.
- Trace schema support is frozen on per-successful-access records for Phase 1.
- Current byte-access `IsBusWait()` omissions are treated as known-gap classifications during replay/diff and are not a blocker for calibration runs.

## 2) Optional trace emission in Ymir

Recommended emission points:

- SH-2 memory access handlers/helpers (ifetch/read/write + MMIO paths)
- SCU DMA access path

Recommended schema is in `docs/trace_format.md`.

### Per-successful-access mode (recommended)

Emit one record after the successful access completes with:

- `seq`
- `master`
- `tick_first_attempt`
- `tick_complete`
- `addr`
- `size`
- `rw`
- `kind`
- `service_cycles`
- `retries`

### Alternate per-attempt mode

Emit one row per attempt (blocked and successful). This lowers patch complexity but increases trace volume and replay parsing complexity.

### Important state note

`tick_first_attempt` and `retries` must survive retries. Because handlers re-enter on retry, this requires a small persistent SH-2 member/state slot (not only local variables).

### Gating

Trace emission should be compile-time and/or runtime gated so disabled mode has near-zero cost.

## 3) Trace format reference

See `docs/trace_format.md`.

## 4) Running trace_replay

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/trace_replay /path/to/bus_trace.jsonl --annotated-output /tmp/annotated.jsonl

# Optional machine-readable summary for calibration scripts
./build/trace_replay /path/to/bus_trace.jsonl --summary-output /tmp/summary.json
```

Optional:

```bash
./build/trace_replay /path/to/bus_trace.jsonl --top 50
```

Sample output excerpt:

```text
records_processed: 10
malformed_lines_skipped: 1
agreement_count: 6
mismatch_count: 2
known_gap_count: 2
delta_histogram:
  Low WRAM | agreement => 3
  VDP2 | mismatch => 1
  SCU regs | known_ymir_wait_model_gap => 1
```

## 5) Interpreting deltas

- `delta_wait == 0` and `delta_total == 0`: replay agreement for that record.
- Non-zero deltas: mismatch between Ymir-derived and arbiter-derived timing.
- `known_ymir_wait_model_gap`: expected discrepancy (example: byte accesses missing `IsBusWait()` path).

Derived Ymir values:

- Exact mode (ticks present and ordered):
  - `ymir_effective_total = tick_complete - tick_first_attempt`
  - `ymir_effective_wait = max(ymir_effective_total - service_cycles, 0)`
- Proxy mode fallback:
  - `ymir_effective_wait = retries * service_cycles`
  - `ymir_effective_total = ymir_effective_wait + service_cycles`

Replay is comparative only. Downstream records keep recorded Ymir ticks even after upstream mismatches.

## 6) What Phase 1 arbiter models / does not model

Modeled:

- Region-based access service timing callback (Ymir-calibrated table)
- Same-address contention penalty
- Tie turnaround penalty
- DMA priority over CPUs
- CPU tie round-robin between MSH2/SSH2

Deferred/not modeled in Phase 1:

- Runtime in-loop Ymir integration
- IF/MA stage coupling model
- Full VDP/SCSP temporal side-effects coupling
- Closed-loop retiming propagation

Known limitation in comparisons:

- Byte-access `IsBusWait()` caveat in current Ymir handlers can produce known-gap classifications.

## Minimal example trace

```json
{"seq":1,"master":"MSH2","tick_first_attempt":10,"tick_complete":12,"addr":"0x02000000","size":4,"rw":"R","kind":"ifetch","service_cycles":2,"retries":0}
{"seq":2,"master":"SSH2","tick_first_attempt":10,"tick_complete":16,"addr":"0x02000000","size":1,"rw":"R","kind":"read","service_cycles":2,"retries":0}
```

Second row can classify as known-gap if arbiter predicts positive wait and Ymir recorded no retries for byte access.
