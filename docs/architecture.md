# Saturnis Architecture (Vertical Slice)

## Core invariants

1. **Committed memory** is owned only by the `BusArbiter`.
2. CPU local view consists of:
   - tiny direct-mapped cache
   - per-CPU store buffer
3. **Memory visibility is arbiter commit order**, not local execution order.
4. Cacheable hits may execute without touching the bus.
5. Uncached/MMIO/cache-miss IFETCH accesses are blocking bus ops.

## Addressing semantics

- Physical address: `phys = vaddr & 0x1FFFFFFF`.
- Uncached alias when `vaddr & 0x20000000`.
- MMIO ranges are always uncached and strongly ordered by bus commit.

## Bus contention model (`bus_free_time`)

The arbiter models a single shared bus domain with `bus_free_time`.

For each committed bus op:

1. `start_time = max(op.req_time, bus_free_time)`
2. `latency = base_latency(region, kind, size) + contention_extra(...)`
3. `finish_time = start_time + latency`
4. Reply stall is `stall_cycles = finish_time - op.req_time`
5. `bus_free_time = finish_time`

The stall is applied to the **current blocking operation** (not deferred to the next op).
CPUs therefore advance virtual time directly from the reply stall, preserving deterministic back-pressure.

## Deterministic arbitration policy

Arbiter order is not host thread submission order.

Selection uses deterministic keys:

1. Minimal computed `start_time`
2. Priority class (`DMA > CPU-MMIO > CPU-RAM` in default policy)
3. Round-robin tie-break for equal-priority CPU-vs-CPU ties using `last_grant_cpu`
4. Final stable tie-break: `(cpu_id, sequence)`

This guarantees identical traces even when producers submit in different host orders.

A producer/arbiter multithread mode uses per-CPU single-producer/single-consumer mailboxes;
ordering remains deterministic because commit selection depends only on emulated-time keys
and policy tie-break rules (never host arrival order).

## Commit safety with progress watermarks

To avoid committing time `T` before all earlier ops can exist, each CPU reports
`executed_up_to`: it will never emit an op with `req_time < executed_up_to`.

The arbiter computes:

- `commit_horizon = min(executed_up_to_cpu0, executed_up_to_cpu1)`

Only ops with `req_time < commit_horizon` are committable when horizon gating is active.
This provides deterministic safety for producer/arbiter decoupling.

Deferred ops that are not yet committable must remain queued and retried; they are never dropped.

## Trace format

Commit trace lines record timing explicitly:

- `t_start`
- `t_end`
- `stall`
- CPU/op/address/value metadata

This makes bus occupancy and WAIT behavior directly inspectable in regression traces.

## Current limitations / TODO

- SH-2 interpreter is intentionally minimal (bring-up subset).
- SH-2 data-memory ops are still skeletal; full store-buffer/cached data-path integration for interpreter ops is TODO.
- Saturn device models are stubs with safe default reads and MMIO logging.
- VDP rendering is placeholder/debug-oriented.
- BIOS execution support is partial and not cycle-accurate.
- `OpBarrier` is implemented as an explicit bus barrier operation (deterministic stall, no memory read/write side effect).
