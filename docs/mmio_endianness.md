# MMIO / RAM Lane Mapping (Current Saturnis Implementation)

Saturnis currently models both RAM and MMIO subword lanes as **big-endian within a 32-bit word**:

- Byte offset `+0` maps to bits `[31:24]`
- Byte offset `+1` maps to bits `[23:16]`
- Byte offset `+2` maps to bits `[15:8]`
- Byte offset `+3` maps to bits `[7:0]`

For halfwords:

- offset `+0` maps to `[31:16]`
- offset `+2` maps to `[15:0]`

## What is enforced by tests

The following microtests lock in this mapping deterministically:

- `test_p0_ram_lane_microtest_longword_to_byte_offsets`
  - writes `0x11223344` to RAM
  - reads bytes at offsets `0..3`
  - expects `0x11, 0x22, 0x33, 0x44`

- `test_p0_mmio_lane_microtest_byte_halfword_and_lane_isolation`
  - writes `0x11223344` to MMIO register `0x05FE0020`
  - verifies byte reads at offsets `0..3` and halfword reads at offsets `0` and `2`
  - verifies a byte write at offset `+1` updates only that lane (`0x11AA3344`)

## Known uncertainty

This mapping is intentionally stable for deterministic emulator behavior and matches the current vertical-slice assumptions.
If later Saturn hardware research proves a different lane convention for specific devices, update only the affected `DeviceHub` lane translation logic and adjust these tests accordingly.
