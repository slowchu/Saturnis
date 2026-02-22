# Saturnis/Ymir Phase 1 Trace Format (JSONL)

This document defines the Phase 1 **offline-first** trace schema used for Saturnis `trace_replay`.
It is designed for deterministic **comparative replay** against Ymir outputs.

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

## Known-gap classification caveat

Some current Ymir byte-sized load/store handlers may bypass `IsBusWait()`. In those cases traces can show `retries=0` for byte accesses even when offline arbiter predicts contention. Replay/diff tooling must classify these as a **known Ymir wait-model gap**, not an arbiter error.

Comparative replay reports should keep this under explicit **known-gap classification** so parity failures remain actionable.

## Alternate mode (optional): per-attempt records

An alternate schema can emit one trace record for each attempt (including blocked attempts), so retries become duplicated records instead of one aggregated success record.

Tradeoff:

- Pros: simpler Ymir patching (less state carried across attempts)
- Cons: noisier traces, larger files, replay parser has to aggregate/interpret attempt streams

Phase 1 recommendation remains per-successful-access records.

## Questions to confirm with Striker before freezing schema

1. Should Phase 1 freeze on per-successful-access records, or support per-attempt records as first-class?
2. Is the current byte-access `IsBusWait()` omission intentional for now, or should it be corrected before calibration runs?
