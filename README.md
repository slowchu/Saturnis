# Saturnis — Determinism-First Sega Saturn Emulator Scaffold

Saturnis is an **open-source Sega Saturn emulator foundation** built to answer a specific question:

> Can we run two SH-2 CPUs in parallel **without lockstep**, while still producing a **bit-for-bit reproducible** global memory/device trace?

This repo is currently a **research / vertical-slice scaffold**. It is not (yet) a full Saturn emulator, but it is designed so correctness and regression confidence can scale as more ISA/device behavior is added.

---

## What Saturnis is doing differently

The Saturn has **two main CPUs (dual SH-2)** that contend for shared RAM and hardware registers. Traditional approaches often rely on lockstep scheduling or heavy synchronization.

Saturnis instead uses **deterministic parallel producers**:

- Each CPU (and later DMA/devices) emits timestamped **bus operations** (`BusOp`) like RAM reads/writes and MMIO reads/writes.
- A single **BusArbiter** is the **only authority** allowed to commit effects to global memory/device state.
- The arbiter enforces a **deterministic total order** based on emulated time and a stable tie-break policy (not host thread timing).
- CPUs can run in parallel and stall via back-pressure (bus occupancy / `bus_free_time`) while still producing identical committed results.

### Determinism is a first-class feature
Saturnis aims for:
- **identical traces across runs**
- **identical traces single-thread vs multi-thread**
- regression tests that fail loudly with minimal “heisenbugs”

---

## Current status (high level)

✅ Implemented / in active use
- Deterministic BusArbiter commit ordering (thread-schedule independent)
- Producer progress / horizon gating to avoid committing “late earlier ops”
- Deterministic fault reporting in traces (illegal op, invalid bus op, contract violations, cache mismatch)
- Store-buffer forwarding + tiny cache for local CPU visibility
- Built-in dual-CPU deterministic demo
- Basic BIOS bring-up entry points (prototype mapping / limited device behavior)

🚧 In progress / next milestones
- SH-2 control-flow correctness expansion (delay-slot edge cases, BSR/BT/BF(/S), PR semantics, exception/interrupt entry/return beyond the scaffold)
- Device behavior growth (SCU interrupts + DMA, VDP2 timing + blanking IRQs, SMPC command/IRQ, VDP1 stepping, CD block FIFO/MMIO, SCSP MMIO front-end)
- Stronger long-run multithread liveness/perf stress tests

❗ Not a goal (right now)
- Full cycle accuracy
- Full graphics/audio fidelity
- “Runs every game today” expectations

---

## Architecture (short version)

- **Producers:** SH-2 cores (and later DMA/devices) generate timestamped `BusOp`s.
- **Single commit authority:** The **BusArbiter** applies the only global writes and resolves read results.
- **Local visibility:** CPUs may see their own pending writes via store-buffer forwarding + tiny cache, but **global visibility happens only at arbiter commit**.
- **MMIO discipline:** Uncached/MMIO operations synchronize through the arbiter with lane/width semantics.

If you’re contributing device work, please read:
- `docs/architecture.md`
- `docs/mmio_endianness.md`

---

## Legal

- No BIOS/ROM content is included.
- You must supply your own Saturn BIOS file.

---

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
## Run
Deterministic built-in dual-CPU demo
```./build/saturnemu --dual-demo --trace demo_trace.jsonl```

BIOS bring-up mode (prototype mapping)
```./build/saturnemu --bios /path/to/your/saturn_bios.bin --headless --trace bios_trace.jsonl```


Note: BIOS bring-up is currently limited by incomplete SH-2 ISA coverage and incomplete device behaviors. The focus is reproducible ordering and debuggable scaffolding.

## Tracing & debugging

Traces are intended to be stable regression fixtures.

Faults are recorded deterministically (e.g., illegal opcode, invalid bus op, cache fill mismatch).

For regression testing, prefer configurations that treat faults as test failures (fail-fast).

## Contributing

### Saturnis is intentionally built in layers. The most helpful contributions are:

- Microtests (small SH-2 programs with expected PC/PR/reg behavior)

- Arbiter/progress invariant tests (parity single vs multi-thread, property tests)

- Device behavior that produces deterministic, testable effects (IRQ/timing/DMA) before fidelity

### PRs that add behavior should also add:

- a microtest or regression fixture

- trace/checkpoint expectations that stay stable across runs

## Project goals (why this exists)

### Saturnis is an experiment in building a Saturn emulator that can:

- run dual SH-2 work in parallel,

- keep a single deterministic “truth” for memory and devices,

- reduce per-game hacks by having stronger ordering/correctness semantics,

- and be easy to debug using repeatable traces.

If that foundation holds, the project can scale from “vertical slice” to “functional emulator” without losing determinism.
