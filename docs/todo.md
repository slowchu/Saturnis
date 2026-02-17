# Saturnis TODO

## Completed backlog

1. [x] Add deterministic VDP2 TVSTAT MMIO semantics (`0x05F80004`) and regression coverage.
2. [x] Expand SCU interrupt register behavior beyond IMS masking (status/pending interactions).
3. [x] Continue SH-2 data-memory path expansion (additional memory op forms + focused tests).
4. [x] Grow BIOS bring-up coverage with deterministic trace assertions.

## Next implementation queue

1. [x] Add deterministic SH-2 delayed-branch behavior coverage (BRA/RTS delay-slot semantics + regression tests).
2. [x] Add focused MMIO interrupt-source wiring into SCU pending bits (deterministic synthetic source model).
3. [ ] Expand BIOS bring-up trace assertions to cover stable state snapshots (PC/register checkpoints).
4. [ ] Add deterministic trace fixture comparison for a fixed BIOS mini-program across single/multirun execution.
5. [ ] Add deterministic MMIO side-effect tracing assertions for SCU synthetic source register transitions.

## Notes

- Keep all device semantics deterministic and test-backed.
- Prefer focused register-level behavior over broad partially-modeled subsystems.
