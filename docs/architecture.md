# Saturnis Architecture (Vertical Slice)

## Core invariants

1. **Committed memory** is owned only by the `BusArbiter`.
2. CPU local view consists of:
   - tiny direct-mapped cache
   - per-CPU store buffer
3. **Memory visibility is arbiter commit order**, not local execution order.
4. Cacheable hits may execute without touching the bus.
5. Uncached or MMIO accesses force synchronization at arbiter commit.

## Addressing semantics

- Physical address: `phys = vaddr & 0x1FFFFFFF`.
- Uncached alias when `vaddr & 0x20000000`.
- MMIO ranges are always uncached and strongly ordered by bus commit.

## Store-buffer forwarding

A CPU immediately sees its own prior writes through the store buffer for matching `(phys, size)` reads.
Global visibility still happens only when the arbiter commits the write.

## Stalls without rollback

Each CPU tracks local request time. A bus commit returns a stall and commit timestamp; CPU local time advances to commit timestamp. Future requests inherit this delay naturally, so no rollback is required.

## Determinism rules

- No wall-clock in emulation core.
- No nondeterministic scheduling dependence: arbiter batch ordering uses `(req_time, cpu_id, sequence)`.
- Regression test compares dual-demo traces byte-for-byte across repeated runs.
- BIOS/runtime bring-up uses per-CPU IFETCH production with arbiter `commit_batch()` ordering rather than fixed lockstep bus commits.
- SH-2 cores can run ahead through local IFETCH cache hits; only misses/uncached fetches emit arbiter bus ops.
- Runtime bounds producer run-ahead by remaining global instruction budget to keep `max_steps` deterministic and strict.
- Interpreter now has a minimal data local-view path for register-indirect `mov.b/mov.w/mov.l` and `@-Rn`/`@Rm+` subset (store buffer forwarding + tiny D-cache), with uncached/MMIO accesses synchronized via arbiter.

## Current limitations / TODO

- SH-2 interpreter is intentionally minimal (bring-up subset).
- SH-2 data-memory semantics are currently implemented for a small register-indirect MOV subset; broader instruction coverage (displacement/GBR forms, atomics, edge cases) is TODO.
- Saturn device models are stubs with safe default reads and MMIO logging.
- VDP rendering is placeholder/debug-oriented.
- BIOS execution support is partial and not cycle-accurate.
- `OpBarrier` is implemented as an explicit bus barrier operation (deterministic stall, no memory read/write side effect).
