# Saturnis (Research Vertical Slice)

Saturnis is a **deterministic Sega Saturn emulator research prototype** focused on a non-lockstep dual SH-2 model:

- Each CPU is a producer of timestamped `BusOp`s.
- A single `BusArbiter` is the authority for committed memory and total order.
- Local CPU visibility uses store-buffer forwarding and a tiny cache.
- Uncached/MMIO accesses synchronize with the arbiter.

## Legal

- No BIOS/ROM content is included.
- You must supply your own Saturn BIOS file.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

Deterministic built-in dual-CPU demo:

```bash
./build/saturnemu --dual-demo --trace demo_trace.jsonl
```

BIOS bring-up mode (simplified mapping in this prototype):

```bash
./build/saturnemu --bios /path/to/your/saturn_bios.bin --headless --trace bios_trace.jsonl
```

## Notes

- This is not a full-accuracy Saturn emulator.
- The implementation prioritizes deterministic bus ordering experiments.
- Optional SDL2 window presentation is compiled in when SDL2 is present.
