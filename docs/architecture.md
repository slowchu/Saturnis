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

## Commit safety with progress watermarks

To avoid committing time `T` before all earlier ops can exist, each CPU reports
`executed_up_to`: it will never emit an op with `req_time < executed_up_to`.

The arbiter computes:

- `commit_horizon = min(executed_up_to_cpu0, executed_up_to_cpu1)`

Only ops with `req_time < commit_horizon` are committable once both CPU progress watermarks exist
(and immediately if tracking was never enabled).
This provides deterministic safety for producer/arbiter decoupling.

## Trace format

Commit trace lines record timing explicitly:

- `t_start`
- `t_end`
- `stall`
- CPU/op/address/value metadata

This makes bus occupancy and WAIT behavior directly inspectable in regression traces.

## Current limitations

- SH-2 interpreter is intentionally minimal (bring-up subset with deterministic IFETCH, MOV #imm, ADD #imm, ADD Rm,Rn, MOV Rm,Rn, BRA/RTS with deterministic delay-slot flow including first-branch-wins branch-in-delay-slot policy and branch-after-slot behavior for MOV.L/MOV.W memory-op delay slots across read/write combinations, including same-address delay-slot-store then target-store overwrite checks plus mixed-width overwrite combinations (documented and regression-tested), and MOV.L/MOV.W data-memory forms).
- SH-2 interpreter now supports blocking MOV.L/MOV.W data-memory read/write execution via deterministic bus/MMIO commits. Direct MMIO-vs-RAM same-address overwrite scenarios are still TODO in this vertical slice because current implemented instruction forms cannot materialize full MMIO base addresses in-register.
- Device models include deterministic semantics for representative SMPC/SCU/VDP/SCSP registers, including SCU IMS/IST mask-pending interactions with synthetic interrupt-source wiring, deterministic MMIO transition logging, trace-ordered MMIO commit assertions, mixed-size contention coverage including overlapping source-clear masks and overlapping same-batch source set/clear operations, and VDP2 TVMD/TVSTAT behavior.
- VDP rendering is placeholder/debug-oriented.
- BIOS execution support is partial and not cycle-accurate, but fixed mini-image bring-up traces are regression-checked for deterministic fixture stability across repeated runs, dual-CPU state-checkpoint consistency, selected commit timing checkpoint stability, MMIO/READ/WRITE/BARRIER commit-count stability, cache-hit true/false count stability, single-thread vs multithread dual-demo per-kind/src parity, and exact mixed-kind commit-prefix stability (with DMA-tagged counts pinned to zero until DMA modeling exists).
- `OpBarrier` is implemented as an explicit bus barrier operation (deterministic stall, no memory read/write side effect).
