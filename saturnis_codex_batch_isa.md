# Saturnis — Codex Task Batch: SH-2 ISA Completeness (Priority Tier 1)

**Objective:** Extend the SH-2 interpreter with the opcode groups most likely to block real
BIOS execution. All tasks must be deterministic, test-backed, and leave the existing test suite
green. Focus is on breadth (unblocking new code paths) not depth (more permutations of covered
behavior).

---

## Task 1 — Implement MOV.L @(disp,PC),Rn (PC-relative longword load)

Add `MOV.L @(disp,PC),Rn` (encoding `0xDndd`). The effective address is
`(PC & ~3) + (disp * 4) + 4`, physically translated via `mem::to_phys`. Emit a blocking
bus `Read` op through the arbiter. On response, sign-extend if needed (longword: no extension)
and write to Rn. Add a deterministic regression: a scripted CPU program that uses
`MOV.L @(disp,PC),Rn` to load a known constant from a nearby memory word, then asserts the
register value and PC advance are correct in the commit trace.

---

## Task 2 — Implement MOV.W @(disp,PC),Rn (PC-relative word load)

Add `MOV.W @(disp,PC),Rn` (encoding `0x9ndd`). The effective address is `PC + (disp * 2) + 4`.
Sign-extend the 16-bit result to 32 bits before writing to Rn. Emit a blocking bus `Read` op.
Add a deterministic regression that loads a known signed word constant via PC-relative
addressing and asserts correct sign extension (both positive and negative values) in the trace.

---

## Task 3 — Add VBR register to SH-2 CPU state

Add `vbr_` (32-bit) to `SH2Core`. Implement `LDC Rm,VBR` (encoding `0x4mFA`) and
`STC VBR,Rn` (encoding `0x002A`). Both are non-memory instructions (no bus op). Add `reset()`
initialization to 0x00000000. Add a deterministic regression: a scripted program loads VBR
from a register, then reads it back via STC, asserting the round-trip value in the CPU
snapshot trace.

---

## Task 4 — Add GBR register to SH-2 CPU state

Add `gbr_` (32-bit) to `SH2Core`. Implement `LDC Rm,GBR` (encoding `0x4mIE`) and
`STC GBR,Rn` (encoding `0x0012`). Both are non-memory. Initialize GBR to 0 on reset. Add a
deterministic regression that verifies LDC/STC round-trip and that GBR is independent of the
general-purpose registers in the CPU snapshot.

---

## Task 5 — Implement pre-decrement stack push: MOV.L Rn,@-Rm

Add `MOV.L Rn,@-Rm` (encoding `0x2nmF`). Pre-decrement Rm by 4, then emit a blocking bus
`Write` op to the new address with the value of Rn. On commit, confirm the write. Rm is
updated before the bus op is emitted (consistent with SH-2 semantics). Add a deterministic
regression: a scripted program pushes two known values to a stack area in RAM and asserts the
committed memory values and the decremented SP in the trace.

---

## Task 6 — Implement pre-decrement push for byte and word: MOV.B Rn,@-Rm / MOV.W Rn,@-Rm

Add `MOV.B Rn,@-Rm` (encoding `0x2nm4`) and `MOV.W Rn,@-Rm` (encoding `0x2nm5`).
Pre-decrement Rm by 1 and 2 respectively, emit appropriately sized bus `Write` ops. Mask the
write value to 8 or 16 bits before sending. Add a deterministic regression covering all three
pre-decrement sizes (byte, word, long) in sequence, asserting committed values and register
post-states.

---

## Task 7 — Implement SUB Rm,Rn

Add `SUB Rm,Rn` (encoding `0x3nm8`). No bus op. Subtract Rm from Rn, write result to Rn, PC
advances by 2. Add a deterministic regression: a scripted sequence that uses ADD and SUB
together (including underflow/wrap cases) and asserts register values in CPU snapshots.

---

## Task 8 — Implement AND, OR, XOR, NOT logical operations

Add the register-register forms:
- `AND Rm,Rn` (`0x2nmA`) — bitwise AND
- `OR Rm,Rn` (`0x2nmB`) — bitwise OR  
- `XOR Rm,Rn` (`0x2nm7`) — bitwise XOR (note: encoding is `0x2nmA`… verify against SH-2 manual)
- `NOT Rm,Rn` (`0x6nmB`) — bitwise NOT of Rm into Rn

None emit bus ops. Add a deterministic regression covering all four ops including edge cases
(zero result, all-bits-set result) with CPU snapshot assertions.

---

## Task 9 — Implement AND #imm,R0 / OR #imm,R0 / XOR #imm,R0 (immediate logical ops)

Add the 8-bit immediate forms operating on R0:
- `AND #imm,R0` (`0xC9ii`)
- `OR #imm,R0` (`0xCBii`)
- `XOR #imm,R0` (`0xCAii`)

Zero-extend the immediate to 32 bits before the operation. Add a deterministic regression using
all three on a known R0 value and asserting the result in CPU snapshots.

---

## Task 10 — Implement conditional branches: BT, BF, BT/S, BF/S

Add:
- `BT disp` (`0x89dd`) — branch if T=1, no delay slot
- `BF disp` (`0x8Bdd`) — branch if T=0, no delay slot
- `BT/S disp` (`0x8Ddd`) — branch if T=1, with delay slot
- `BF/S disp` (`0x8Fdd`) — branch if T=0, with delay slot

Displacement is sign-extended 8-bit, effective address = PC + (disp*2) + 4. For /S variants,
execute the delay slot instruction before taking/not-taking the branch (same delay-slot
mechanics as existing BRA). Add deterministic regressions for: taken branch, not-taken branch,
/S variants (both taken and not-taken), and a T-flag interaction test combining CMP/EQ with BT.

---

## Task 11 — Implement real exception entry using VBR (replacing synthetic scaffold)

Upgrade the exception entry path to use VBR. When `request_exception_vector(v)` fires:
1. Decrement R15 (SP) by 4 and emit a bus `Write` of SR to the new SP.
2. Decrement R15 by 4 again and emit a bus `Write` of PC+2 to the new new SP.
3. Fetch the handler address from `VBR + (v * 4)` via a bus `Read`.
4. Set PC to the fetched handler address; set the I-bits in SR to mask the triggering level.

Mark the existing `SYNTHETIC_EXCEPTION_ENTRY` trace label as legacy; new entries emit
`EXCEPTION_ENTRY` with VBR address in the fault payload. Preserve existing synthetic tests by
keeping the label distinct. Add a deterministic regression: a scripted program triggers a
synthetic exception, asserts the correct stack pushes appear as committed writes, and asserts
PC jumps to the handler address read from the VBR table.

---

## Task 12 — Implement real RTE (replacing synthetic scaffold)

Upgrade `RTE` (`0x002B`) to use the stack:
1. Emit a bus `Read` from R15 (SP), write result to PC. Increment R15 by 4.
2. Emit a bus `Read` from R15, write result to SR. Increment R15 by 4.

RTE has a delay slot (execute one instruction before returning). Emit `EXCEPTION_RETURN` trace
label instead of `SYNTHETIC_RTE`. Add a deterministic regression: enter a synthetic exception
using the Task 11 path, execute RTE, and assert that PC and SR are correctly restored from the
stack in the commit trace.

---

## Task 13 — Implement @(disp,Rn) displacement addressing for MOV.L / MOV.W / MOV.B

Add the register+displacement addressing forms:
- `MOV.L @(disp,Rn),Rm` (`0x5nmF` — 4-bit disp, scaled by 4)
- `MOV.L Rm,@(disp,Rn)` (`0x1nmF` — 4-bit disp, scaled by 4)
- `MOV.W @(disp,Rn),R0` (`0x85nd` — 4-bit disp, scaled by 2, dest always R0)
- `MOV.B @(disp,Rn),R0` (`0x84nd` — 4-bit disp, unscaled, dest always R0)

Each emits a blocking bus op through the arbiter at the computed address. Add a deterministic
regression covering read and write forms at multiple displacements, asserting committed values
and register results in the trace.

---

## Task 14 — Add MACH and MACL registers; implement MUL.L Rm,Rn

Add `mach_` and `macl_` (32-bit each) to `SH2Core`, initialized to 0 on reset. Add `STC
MACH,Rn` / `STC MACL,Rn` and `LDC Rn,MACH` / `LDC Rn,MACL`. Implement `MUL.L Rm,Rn`
(`0x0nmF`) — 32×32→64-bit unsigned multiply, result low 32 bits to MACL, high 32 bits to MACH.
This is a multi-cycle instruction on real hardware; model it as 2 ticks of local time per the
vertical-slice convention. Add a deterministic regression: multiply two known 32-bit values,
read MACL and MACH via STC, assert both halves of the 64-bit product.

---

## Task 15 — Implement EXTS.B, EXTS.W, EXTU.B, EXTU.W (sign/zero extension)

Add:
- `EXTS.B Rm,Rn` (`0x600CU`) — sign-extend byte of Rm to 32 bits → Rn  (verify encoding: likely `0x6nmE` or similar — check SH-2 manual)
- `EXTS.W Rm,Rn` — sign-extend word
- `EXTU.B Rm,Rn` — zero-extend byte
- `EXTU.W Rm,Rn` — zero-extend word

No bus ops. Add a deterministic regression covering all four forms with both positive and
negative source values, asserting correct extension behavior in CPU snapshots.

---

## Task 16 — Refresh architecture docs and run full regression pass

1. Update `docs/architecture.md` to document the new VBR/GBR registers, real exception entry/
   RTE semantics, and the expanded addressing mode set.
2. Update `docs/todo.md` to mark Tasks 1–15 complete and populate a new next-batch section
   covering: `BSR`, `JMP`, `@(R0,Rn)` indexed addressing, `MULS.W`/`MULU.W`, `DIV0U`/`DIV1`,
   `@(disp,GBR)` addressing, `TRAPA` (software interrupt), and BIOS bring-up trace progress
   measurement (first N instructions before first ILLEGAL_OP).
3. Run the full test suite and confirm all existing determinism invariants (ST/MT trace parity,
   commit-prefix stability, SCU/BIOS fixture stability) remain green.
4. Run the BIOS bring-up mode (`--bios ... --headless --trace`) and record how many instructions
   execute before the first `ILLEGAL_OP` fault, committing this count as a regression baseline
   fixture so future batches can track forward progress numerically.

---

## Notes for Codex

- All new instructions follow the existing pattern: decode in `execute_instruction()` for
  non-memory ops; emit pending `BusOp` from `produce_until_bus()` for memory ops.
- Verify all opcode encodings against the Hitachi SH-7604 Hardware Manual before implementing.
- Every task must include at least one new deterministic test that would fail if the
  implementation were wrong or absent.
- Do not remove or weaken any existing test assertions.
- The ILLEGAL_OP fault path must remain in place as the catch-all for unimplemented opcodes.
