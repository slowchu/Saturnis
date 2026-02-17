# Saturnis TODO

## Active backlog

1. [x] Add deterministic VDP2 TVSTAT MMIO semantics (`0x05F80004`) and regression coverage.
2. [ ] Expand SCU interrupt register behavior beyond IMS masking (status/pending interactions).
3. [ ] Continue SH-2 data-memory path expansion (additional memory op forms + focused tests).
4. [ ] Grow BIOS bring-up coverage with deterministic trace assertions.

## Notes

- Keep all device semantics deterministic and test-backed.
- Prefer focused register-level behavior over broad partially-modeled subsystems.
