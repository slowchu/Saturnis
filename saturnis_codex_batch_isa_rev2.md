# Saturnis — Codex Task Batch: SH-2 ISA Completeness (Priority Tier 1, Rev 3)

**Objective:** Extend the SH-2 interpreter with the opcode groups most likely to block real BIOS execution.
All tasks must be deterministic, test-backed, and keep the existing test suite green. Focus on breadth
(unblocking new code paths) not depth (exploring every permutation of already-covered behavior).

**Global constraints (apply to all tasks):**
- No silent NOP fallback for unknown opcodes. Unsupported opcode must produce a deterministic ILLEGAL_OP fault marker.
- Preserve ST/MT trace parity invariants.
- New bus-visible behavior must flow through the BusArbiter (blocking ops for now).
- Every task must add at least one deterministic regression test.

---

## PC / Branch Addressing Convention (applies broadly)
> **In Saturnis, `pc_` in `execute_instruction()` is the address of the current 16-bit opcode.**
> - The “PC-relative base” for `@(disp,PC)` forms is `pc_ + 4`.
> - Branch targets for disp-based branches are computed from `pc_ + 4` plus signed disp scaling.
> - Sequential fall-through after executing an instruction at `pc_` is `pc_ + 2`.

If this convention changes, update the task formulas and tests accordingly.

---

## Task 1 — Implement MOV.L @(disp,PC),Rn (PC-relative longword load)

Add `MOV.L @(disp,PC),Rn` (encoding `0xDndd`).
Effective address:
`((pc_ + 4) & ~3) + (disp * 4)` (note longword base alignment).

Emit a blocking bus `Read` op (4 bytes) through the arbiter.
On response, write the 32-bit result to Rn (no sign extension). Advance PC by 2.

**Regression:**
- Place a known 32-bit constant at a PC-relative offset and load it.
- Assert register value, committed Read op address, and PC advance.
- Include a case that crosses the `& ~3` alignment boundary.

---

## Task 2 — Implement MOV.W @(disp,PC),Rn (PC-relative word load)

Add `MOV.W @(disp,PC),Rn` (encoding `0x9ndd`).
Effective address:
`(pc_ + 4) + (disp * 2)` (no extra alignment mask).

Emit a blocking bus `Read` op (2 bytes).
Sign-extend the 16-bit result to 32 bits before writing to Rn. Advance PC by 2.

**Regression:**
- One positive word (no sign-ext), one negative word (sign-ext fires).
- Two displacements.
- Assert register values and PC.

---

## Task 3 — Implement MOVA @(disp,PC),R0 (PC-relative address materialization)

Add `MOVA @(disp,PC),R0` (encoding `0xC7dd`).
This does NOT read memory; it computes an address into R0:
`((pc_ + 4) & ~3) + (disp * 4)`.

No bus op. Advance PC by 2.

**Regression:**
- Use MOVA to compute an address, then use that address in a subsequent MOV.L @Rm,Rn load.
- Assert R0 equals the expected address and the load reads the expected value.

---

## Task 4 — Add VBR register; implement STC VBR,Rn and LDC Rm,VBR

Add `vbr_` (32-bit) to `SH2Core`, init to 0 on reset.

Implement:
- `STC VBR,Rn` = `0000nnnn00100010` = `0x0n22` (no bus op)
- `LDC Rm,VBR` = `0100mmmm00101110` = `0x4m2E` (no bus op)

**Regression:**
- Load VBR via LDC, read back via STC, assert round-trip.
- Confirm VBR is independent of GPRs by setting unrelated registers and verifying VBR unchanged.

---

## Task 5 — Add GBR register; implement STC GBR,Rn and LDC Rm,GBR

Add `gbr_` (32-bit) to `SH2Core`, init to 0 on reset.

Implement:
- `STC GBR,Rn` = `0000nnnn00010010` = `0x0n12` (no bus op)
- `LDC Rm,GBR` = `0100mmmm00011110` = `0x4m1E` (no bus op)

**Regression:**
- LDC/STC round-trip for GBR.
- Verify GBR and VBR are independent: set distinct values; read both back; assert no cross-contamination.

---

## Task 6 — Implement pre-decrement stores: MOV.L/W/B Rm,@-Rn

Add the three pre-decrement write forms (note operand roles):
- `MOV.B Rm,@-Rn` = `0010nnnnmmmm0100` = `0x2nm4` (Rn -= 1; store low 8 bits of Rm)
- `MOV.W Rm,@-Rn` = `0010nnnnmmmm0101` = `0x2nm5` (Rn -= 2; store low 16 bits of Rm)
- `MOV.L Rm,@-Rn` = `0010nnnnmmmm0110` = `0x2nm6` (Rn -= 4; store 32-bit Rm)

Pre-decrement Rn **before** emitting the bus `Write` op. Mask value to 8/16 bits for byte/word.

**Regression:**
- Use R15 as stack pointer.
- Push byte/word/long values with the three forms.
- Pop them back using existing post-increment loads (`MOV.* @Rm+,Rn` with m=15).
- Assert committed memory contents and final R15.

(Include an edge-case regression where `m == n` is NOT used here—pre-decrement uses different semantics than post-inc load special-casing.)

---

## Task 7 — Implement SUB Rm,Rn and SUBC / SUBV

Implement:
- `SUB Rm,Rn`  = `0010nnnnmmmm1000` = `0x2nm8`
- `SUBC Rm,Rn` = `0011nnnnmmmm1010` = `0x3nmA` (borrow in/out via T)
- `SUBV Rm,Rn` = `0011nnnnmmmm1011` = `0x3nmB` (overflow sets T)

No bus ops.

**Regression:**
- Basic subtract.
- Underflow/wrap check.
- SUBC two-step borrow chain (T propagation).
- SUBV overflow boundary cases.

---

## Task 8 — Implement AND, OR, XOR, NOT (register-register)

Implement:
- `AND Rm,Rn` = `0010nnnnmmmm1001` = `0x2nm9`
- `XOR Rm,Rn` = `0010nnnnmmmm1010` = `0x2nmA`
- `OR  Rm,Rn` = `0010nnnnmmmm1011` = `0x2nmB`
- `NOT Rm,Rn` = `0110nnnnmmmm0111` = `0x6nm7`

No bus ops.

**Regression:**
- Known inputs for each op (zero result, all-ones result, NOT of zero, etc.).

---

## Task 9 — Implement immediate logical ops on R0 + GBR-relative byte RMW

Immediate-on-R0 forms (zero-extend imm8 to 32-bit):
- `AND #imm,R0` = `0xC9ii`
- `XOR #imm,R0` = `0xCAii`
- `OR  #imm,R0` = `0xCBii`

GBR-relative byte read-modify-write forms (requires Task 5 / GBR):
- `AND.B #imm,@(R0,GBR)` = `0xCCii`
- `XOR.B #imm,@(R0,GBR)` = `0xCEii`
- `OR.B  #imm,@(R0,GBR)` = `0xCFii`

For the `.B @(R0,GBR)` forms:
- Compute EA = GBR + R0 (byte address).
- Emit bus Read(1), compute new byte, emit bus Write(1).

**Regression:**
- R0 immediate ops: assert R0 result.
- GBR-relative RMW: assert commit sequence (Read then Write) and final byte value in memory.

---

## Task 10 — Implement conditional branches: BT, BF, BT/S, BF/S

Implement:
- `BT  disp8`  = `0x89dd` (branch if T=1, no delay slot)
- `BF  disp8`  = `0x8Bdd` (branch if T=0, no delay slot)
- `BT/S disp8` = `0x8Ddd` (branch if T=1, with delay slot)
- `BF/S disp8` = `0x8Fdd` (branch if T=0, with delay slot)

Target address for taken branches:
`target = (pc_ + 4) + (signext(disp8) * 2)`

PC behavior:
- BT/BF (no /S): if taken -> PC = target; else -> PC = pc_ + 2.
- BT/S, BF/S: always execute the delay-slot instruction at `pc_ + 2`.
  - If taken -> after executing slot, set PC = target.
  - If not taken -> after executing slot, fall through to PC = pc_ + 4.

Use the same delay-slot mechanism as BRA/JSR/RTS.

**Regression:**
- BT taken / not-taken (use CMP/EQ to drive T).
- BF taken / not-taken.
- BT/S taken: confirm slot executes before branch takes effect.
- BF/S not-taken: confirm slot executes and then fall-through occurs.

---

## Task 11 — Implement additional CMP variants commonly used in firmware

Add:
- `CMP/HS Rm,Rn` = `0x3nm2` (unsigned Rn >= Rm)
- `CMP/GE Rm,Rn` = `0x3nm3` (signed   Rn >= Rm)
- `CMP/HI Rm,Rn` = `0x3nm6` (unsigned Rn >  Rm)
- `CMP/GT Rm,Rn` = `0x3nm7` (signed   Rn >  Rm)
- `CMP/PL Rn`    = `0x4n15` (signed Rn > 0)
- `CMP/PZ Rn`    = `0x4n11` (signed Rn >= 0)
- `CMP/STR Rm,Rn`= `0x2nmC` (byte-wise compare: T=1 if ANY corresponding byte matches)

No bus ops.

**Regression:**
- Boundary cases: equal vs GT/GE, zero vs PL/PZ, STR byte-match positive and negative examples.

---

## Task 12 — Implement TRAPA #imm (software interrupt)

Add `TRAPA #imm` (encoding `0xC3ii`).

TRAPA exception entry (stack-based, requires Task 4 VBR + Task 6 pre-decrement):
1. Push SR: `R15 -= 4; write32(SR, @R15)`
2. Push return PC: `R15 -= 4; write32(pc_ + 2, @R15)` (return to instruction after TRAPA)
3. Fetch handler address: `handler = read32(VBR + (imm * 4))`
4. Set PC to handler (no implicit +4 bias)
5. (If you model SR interrupt mask changes for exceptions, apply consistently here; if not, leave TODO but deterministic.)

Emit bus writes for the pushes and a bus read for the vector fetch.

**Regression:**
- Build a tiny vector table in memory.
- Execute TRAPA; confirm stack writes occur in-order and PC becomes handler.
- Handler executes RTE; confirm return PC resumes at post-TRAPA instruction.

---

## Task 13 — Implement real exception entry + real RTE using VBR + stack

Upgrade `request_exception_vector(v)` from synthetic to stack-based exception entry.
Define the contract: exceptions are taken at an instruction boundary, so the “next instruction PC” is `pc_`.

Exception entry:
1. `R15 -= 4; write32(SR, @R15)`
2. `R15 -= 4; write32(pc_, @R15)`  (return to the next instruction)
3. `pc_ = read32(VBR + (v * 4))`
4. (If modeling SR.I mask level: update SR deterministically; otherwise leave TODO)

Upgrade `RTE` (`0x002B`) to real restore with delay slot:
1. Pop PC: `new_pc = read32(@R15); R15 += 4`
2. Pop SR: `new_sr = read32(@R15); R15 += 4`
3. Execute the delay-slot instruction at `pc_ + 2` (per SH-2 RTE delay-slot behavior)
4. After slot, commit `pc_ = new_pc`, `sr_ = new_sr`

Trace:
- Emit distinct `EXCEPTION_ENTRY` and `EXCEPTION_RETURN` markers.
- Keep old synthetic markers recognized by tests, but new code paths must not generate them.

**Regression:**
- Trigger a test exception via `request_exception_vector`.
- Assert stack commit order (SR then PC), handler fetch via VBR.
- Execute RTE and assert restored PC/SR.

---

## Task 14 — Implement @(disp,Rm) displacement addressing (loads + stores)

Implement the 4-bit displacement forms.

Longword forms (disp4 scaled by 4):
- `MOV.L @(disp,Rm),Rn` = `0101nnnnmmmmdddd` = `0x5nmd`
  EA = Rm + (disp * 4); Read32 -> Rn
- `MOV.L Rm,@(disp,Rn)` = `0001nnnnmmmmdddd` = `0x1nmd`
  EA = Rn + (disp * 4); Write32 from Rm

R0-only byte/word forms:
- `MOV.W @(disp,Rm),R0` = `0x85md` (disp4 scaled by 2; sign-extend)
- `MOV.B @(disp,Rm),R0` = `0x84md` (disp4 unscaled; sign-extend)
- `MOV.W R0,@(disp,Rn)` = `0x81nd` (disp4 scaled by 2)
- `MOV.B R0,@(disp,Rn)` = `0x80nd` (disp4 unscaled)

Each emits a blocking bus op through the arbiter (Read or Write).
Sign-extend byte/word loads.

**Regression:**
- Test disp=0 and disp>0.
- Test sign extension for byte/word loads (positive and negative).
- Round-trip write then read at displacement.

---

## Task 15 — Implement MUL.L and add MACH/MACL registers + transfers

Add `mach_` and `macl_` (32-bit) to `SH2Core`, init to 0 on reset.

Implement:
- `MUL.L Rm,Rn` = `0000nnnnmmmm0111` = `0x0nm7`
  Operation: signed 32×32 multiply; **store low 32 bits into MACL only**.
  MACH is not modified by MUL.L.

Also implement:
- `STS MACL,Rn` = `0000nnnn00011010` = `0x0n1A`
- `STS MACH,Rn` = `0000nnnn00001010` = `0x0n0A`
- `LDS Rm,MACL` = `0100mmmm00011010` = `0x4m1A`
- `LDS Rm,MACH` = `0100mmmm00001010` = `0x4m0A`

Model MUL.L as 2 ticks of local time (vertical-slice convention).

**Regression:**
- Multiply two known values; verify MACL matches expected low-32 result via STS.
- Include a check that MACH is unchanged by MUL.L.

---

## Task 16 — Docs refresh + full regression + BIOS forward-progress baseline (local-only)

1. Update `docs/architecture.md` to document:
   - VBR/GBR/MACH/MACL registers
   - PC-relative base rule and branch target formulas
   - Real exception entry + real RTE semantics
   - Newly supported addressing modes (`@(disp,PC)`, MOVA, pre-decrement, `@(disp,Rm)`)

2. Update `docs/todo.md`:
   - Mark Tasks 1–15 complete.
   - Add a “Next batch candidates” list (do not implement in this batch):
     `BSR`, `JMP @Rm`, `@(R0,Rn)` indexed, `@(disp,GBR)`, `EXTS/EXTU`, `NEG`,
     `SHLL2/8/16`, `DMULS/DMULU`, `MULS/MULU`, `DIV0U/DIV1`.

3. Run full test suite; confirm determinism invariants remain green.

4. **Local-only BIOS progress metric (not CI):**
   - Run: `--bios /path/to/bios.bin --headless --trace bios_progress.jsonl`
   - Record:
     (a) instruction count executed before first ILLEGAL_OP fault
     (b) opcode + PC at first fault
   - Store the count/opcode/pc as a local baseline artifact (e.g., docs note or ignored file),
     so devs can track progress without requiring BIOS in CI.

