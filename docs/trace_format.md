# Saturnis/Ymir Phase 1 Trace Format (JSONL)

This document defines the Phase 1 **offline-first** trace schema used for Saturnis `trace_replay`.

## Recommended schema (per-successful-access)

One JSON object per successful access, one object per line.

Required fields:

- `seq` (`uint64`): monotonic sequence number.
- `master` (`"MSH2" | "SSH2" | "DMA"`)
- `tick_first_attempt` (`uint64`)
- `tick_complete` (`uint64`)
- `addr` (hex string, e.g. `"0x06004000"`)
- `size` (`1 | 2 | 4`)
- `rw` (`"R" | "W"`)
- `kind` (`"ifetch" | "read" | "write" | "mmio_read" | "mmio_write"`)
- `service_cycles` (`uint32`): **per-attempt** `AccessCycles()` result used by Ymir.
- `retries` (`uint32`): blocked attempts before success.

Example:

```json
{"seq":1,"master":"MSH2","tick_first_attempt":1042,"tick_complete":1044,"addr":"0x06004000","size":4,"rw":"R","kind":"ifetch","service_cycles":2,"retries":0}
```

## Semantics

- `service_cycles` is **not a stall field**. It is the service cost for one granted attempt.
- `retries` counts blocked retries before the successful grant.
- There is **no raw `stall` field** in Phase 1 schema.
- Tooling derives:
  - `ymir_effective_total`
  - `ymir_effective_wait`
- `retries * service_cycles` is a **proxy metric**, not guaranteed exact elapsed wait, because scheduler/stepping cadence influences retry timing.
- `tick_first_attempt` + `tick_complete` allow exact elapsed derivation when available.
- `seq` is the authoritative deterministic tie-break when completion ticks are equal.
- `seq` is expected to be monotonic in emission order for schema quality; replay tool reports `non_monotonic_seq_count` / `duplicate_seq_count` diagnostics but continues processing.

## Known Ymir behavior caveat

Some current Ymir byte-sized load/store handlers may bypass `IsBusWait()`. In those cases traces can show `retries=0` for byte accesses even when offline arbiter predicts contention. Replay/diff tooling must classify these as a **known Ymir wait-model gap**, not an arbiter error.

## Alternate mode (optional): per-attempt records

An alternate schema can emit one trace record for each attempt (including blocked attempts), so retries become duplicated records instead of one aggregated success record.

Tradeoff:

- Pros: simpler Ymir patching (less state carried across attempts)
- Cons: noisier traces, larger files, replay parser has to aggregate/interpret attempt streams

Phase 1 recommendation remains per-successful-access records.

## Phase 1 fixed assumptions

1. Phase 1 is frozen on **per-successful-access** records as the primary/required schema.
2. Per-attempt format remains optional/future and is not first-class in current tooling.
3. Byte-size `IsBusWait()` omission in current Ymir is treated as a **known Ymir wait-model gap** in replay/diff output, not an arbiter error and not a blocker for calibration runs.
4. Replay summaries should report known-gap frequency so impact can be measured and revisited later if needed.

## Binary format (BTR1 v1)

`trace_replay` now also accepts a compact binary format for large captures.

- Header: 8 bytes
  - magic: `BTR1` (4 bytes)
  - version: little-endian `uint16` (`1`)
  - record_size: little-endian `uint16` (`48`)
- Record: 48 bytes, little-endian
  - `uint64 seq`
  - `uint64 tick_first_attempt`
  - `uint64 tick_complete`
  - `uint32 addr`
  - `uint32 service_cycles`
  - `uint32 retries`
  - `uint8 master` (`0=MSH2`, `1=SSH2`, `2=DMA`)
  - `uint8 rw` (`0=R`, `1=W`)
  - `uint8 size` (`1|2|4`)
  - `uint8 kind` (`0=ifetch`, `1=read`, `2=write`, `3=mmio_read`, `4=mmio_write`)
  - `uint32 reserved0`
  - `uint32 reserved1`

Validation behavior:
- invalid magic/version/record size => hard error
- truncated header/record => hard error
- malformed record enum/size => warning + skip record
