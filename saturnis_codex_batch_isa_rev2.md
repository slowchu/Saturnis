# Saturnis — Codex Task Batch: SH-2 ISA Completeness (Priority Tier 1, Rev 2)

**Objective:** Extend the SH-2 interpreter with the opcode groups most likely to block real
BIOS execution. All tasks must be deterministic, test-backed, and leave the existing test suite
green. Focus is on breadth (unblocking new code paths) not depth (more permutations of covered
behavior).

---

## PC-Relative Addressing Base Rule (applies to Tasks 1, 2, and 16)

> **In Saturnis, `pc_` in `execute_instruction()` is the address of the current 16-bit opcode.**
> PC-relative addressing uses `pc_ + 4` as the base address for effective address calculation.
> This matches the SH-2 manual's description: "the PC points to the starting address of the
> second instruction after the current instruction."
> Without this rule, PC-relative loads produce off-by-4 addresses that manifest as
> apparently-random BIOS failures.

---

## Task 1 — Implement MOV.L @(disp,PC),Rn (PC-relative longword load)

Add `MOV.L @(disp,PC),Rn` (encoding `0xDndd`). Effective address:
`((pc_ + 4) & ~3) + (disp * 4)` — note the longword-alignment mask on the base.
Emit a blocking bus `Read` op (4 bytes) through the arbiter. On response, write the 32-bit
result to Rn (no sign extension for longword). Advance PC by 2.
Add a deterministic regression: a scripted CPU program places a known 32-bit constant in
memory at a PC-relative offset, loads it with `MOV.L @(disp,PC),Rn`, and asserts the register
value and PC advance in the commit trace. Test both aligned and the alignment-mask boundary.

---

## Task 2 — Implement MOV.W @(disp,PC),Rn (PC-relative word load)

Add `MOV.W @(disp,PC),Rn` (encoding `0x9ndd`). Effective address:
`(pc_ + 4) + (disp * 2)` — no alignment mask for word form.
Sign-extend the 16-bit result to 32 bits before writing to Rn. Emit a blocking bus `Read` op
(2 bytes). Advance PC by 2.
Add a deterministic regression covering: a positive constant (no sign extension fires), a
negative constant (sign extension fires), and two different displacements, all asserting correct
register values in CPU snapshots.

---

## Task 3 — Implement MOVA @(disp,PC),R0 (PC-relative address materialization)

Add `MOVA @(disp,PC),R0` (encoding `0xC7dd`). This instruction does NOT access memory —
it computes an address and loads it into R0. Effective address:
`((pc_ + 4) & ~3) + (disp * 4)` — same longword-aligned base as `MOV.L @(disp,PC)`.
No bus op. Advance PC by 2.
Add a deterministic regression: compute a known base address using MOVA, then use the result
register to perform a MOV.L load, asserting both the address in R0 and the loaded value.
(MOVA is used constantly in position-independent SH-2 code for address tables and jump vectors.)

---

## Task 4 — Add VBR register; fix STC VBR,Rn and LDC Rm,VBR encodings

Add `vbr_` (32-bit) to `SH2Core`, initialized to 0x00000000 on `reset()`.

Implement with correct encodings per the SH-2 manual:
- `STC VBR,Rn` = `0000nnnn00100010` = `0x0n22` — copies VBR to Rn (no bus op)
- `LDC Rm,VBR` = `0100mmmm00101110` = `0x4m2E` — copies Rm to VBR (no bus op)

Note: `0x0n2A` is `STS PR,Rn`, not `STC VBR,Rn` — do not confuse these.

Add a deterministic regression: load a known value into VBR via `LDC`, read it back via `STC`,
assert the round-trip value in the CPU snapshot. Also assert VBR is independent of all
general-purpose registers.

---

## Task 5 — Add GBR register; implement STC GBR,Rn and LDC Rm,GBR

Add `gbr_` (32-bit) to `SH2Core`, initialized to 0x00000000 on `reset()`.

Implement with correct encodings:
- `STC GBR,Rn` = `0000nnnn00010010` = `0x0n12` — copies GBR to Rn (no bus op)
- `LDC Rm,GBR` = `0100mmmm00011110` = `0x4m1E` — copies Rm to GBR (no bus op)

Add a deterministic regression: verify LDC/STC round-trip for GBR, and verify GBR and VBR
are independently addressable (load distinct values into each, read back both, assert no
cross-contamination).

---

## Task 6 — Implement pre-decrement stack operations: MOV.L/W/B Rn,@-Rm

Add the three pre-decrement write forms:
- `MOV.L Rn,@-Rm` (`0x2nmF`) — pre-decrement Rm by 4, write 32-bit Rn to new address
- `MOV.W Rn,@-Rm` (`0x2nm5`) — pre-decrement Rm by 2, write low 16 bits of Rn
- `MOV.B Rn,@-Rm` (`0x2nm4`) — pre-decrement Rm by 1, write low 8 bits of Rn

Pre-decrement Rm before emitting the bus `Write` op (consistent with SH-2 semantics: address
update happens before the write). Mask the write value to 16 or 8 bits for word/byte forms.

Add a deterministic regression: a scripted program pushes three values of different widths
onto a RAM stack area, then reads them back, asserting committed memory contents and the final
SP value in the trace.

---

## Task 7 — Implement SUB Rm,Rn and SUBC / SUBV (if needed for BIOS)

Add:
- `SUB Rm,Rn` = `0010nnnnmmmm1000` = `0x2nm8` — subtract Rm from Rn, result to Rn (no bus op)
- `SUBC Rm,Rn` = `0011nnnnmmmm1010` = `0x3nmA` — subtract with borrow (T-flag in, T-flag out)
- `SUBV Rm,Rn` = `0011nnnnmmmm1011` = `0x3nmB` — subtract with overflow detection into T

Add a deterministic regression covering: basic subtract, underflow/wrap, SUBC carry chain
across two operations, and SUBV overflow detection, all with CPU snapshot assertions.

---

## Task 8 — Implement AND, OR, XOR, NOT with correct encodings

Add the register-register logical forms with correct SH-2 manual encodings:
- `AND Rm,Rn` = `0010nnnnmmmm1001` = `0x2nm9`
- `XOR Rm,Rn` = `0010nnnnmmmm1010` = `0x2nmA`
- `OR Rm,Rn`  = `0010nnnnmmmm1011` = `0x2nmB`
- `NOT Rm,Rn` = `0110nnnnmmmm0111` = `0x6nm7`

Note: `0x2nmA` is XOR (not AND), and `0x6nm7` is NOT (not `0x6nmB`).

None emit bus ops. Add a deterministic regression covering all four operations with known
inputs, including: zero result, all-bits-set result, NOT of zero, NOT of all-ones, all with
CPU snapshot assertions.

---

## Task 9 — Implement AND/OR/XOR immediate forms operating on R0

Add the 8-bit immediate logical ops (zero-extend immediate to 32 bits):
- `AND #imm,R0` = `0xC9ii`
- `XOR #imm,R0` = `0xCAii`
- `OR #imm,R0`  = `0xCBii`

Also add the GBR-relative byte logical forms (commonly used for bit manipulation in BIOS):
- `AND.B #imm,@(R0,GBR)` = `0xCCii` — read-modify-write byte at GBR+R0
- `XOR.B #imm,@(R0,GBR)` = `0xCEii`
- `OR.B  #imm,@(R0,GBR)` = `0xCFii`

The GBR-relative forms require Task 5 (GBR). They emit a bus `Read` op followed by a bus
`Write` op for the modified byte. Add a deterministic regression for all immediate forms,
including a GBR-relative read-modify-write that asserts both the bus commit sequence and the
final memory value.

---

## Task 10 — Implement conditional branches: BT, BF, BT/S, BF/S

Add all four conditional branch forms:
- `BT  disp8` = `0x89dd` — branch if T=1, no delay slot; target = `pc_ + 4 + (disp * 2)`
- `BF  disp8` = `0x8Bdd` — branch if T=0, no delay slot
- `BT/S disp8` = `0x8Ddd` — branch if T=1, with delay slot
- `BF/S disp8` = `0x8Fdd` — branch if T=0, with delay slot

For not-taken branches, PC advances by 2 (no delay slot for BT/BF; for BT/S and BF/S, execute
the delay slot and then advance PC by 2 rather than branching). Use the same delay-slot
mechanics as the existing BRA implementation.

Add deterministic regressions for:
1. BT taken and not-taken (using CMP/EQ to set T)
2. BF taken and not-taken
3. BT/S taken — confirm delay-slot instruction executes before branch target
4. BF/S not-taken — confirm delay-slot instruction executes before fall-through

---

## Task 11 — Implement CMP variants needed for conditional branch tests

The existing `CMP/EQ` forms are present. Add the remaining CMP forms needed by BIOS:
- `CMP/HS Rm,Rn` = `0x3nm2` — T=1 if Rn >= Rm (unsigned)
- `CMP/GE Rm,Rn` = `0x3nm3` — T=1 if Rn >= Rm (signed)
- `CMP/HI Rm,Rn` = `0x3nm6` — T=1 if Rn > Rm (unsigned)
- `CMP/GT Rm,Rn` = `0x3nm7` — T=1 if Rn > Rm (signed)
- `CMP/PL Rn`    = `0x4n15` — T=1 if Rn > 0 (signed)
- `CMP/PZ Rn`    = `0x4n11` — T=1 if Rn >= 0 (signed)
- `CMP/STR Rm,Rn`= `0x2nmC` — T=1 if any byte of Rn equals corresponding byte of Rm

Add a deterministic regression covering each variant, including boundary cases (equal values
for GT vs GE, zero for PL vs PZ, byte-match for STR).

---

## Task 12 — Implement TRAPA #imm (software interrupt / syscall)

Add `TRAPA #imm` (encoding `0xC3ii`). Stack layout per the SH-2 manual:
1. `R15 -= 4; write(SR, @R15)` — push SR first
2. `R15 -= 4; write(PC - 2, @R15)` — push return PC (address of the TRAPA instruction itself,
   i.e., `pc_` before the +2 advance; verify against your PC bookkeeping convention)
3. `PC = read(VBR + (imm * 4)) + 4` — fetch handler from VBR table, bias by +4

Note: the saved PC value is the address of the TRAPA instruction so that RTE (which restores
PC from stack and adds the standard +2 advance) returns correctly to the instruction after
TRAPA. Verify this against Saturnis's PC bookkeeping before implementing.

TRAPA requires Tasks 4 (VBR) and 6 (pre-decrement push) as prerequisites.

Emit bus writes for the two stack pushes and a bus read for the vector fetch, all through the
arbiter. Emit a `TRAPA` trace label with the imm value and handler address.

Add a deterministic regression: a scripted program executes TRAPA, the vector table directs it
to a handler stub, the handler executes RTE, and the trace asserts correct stack commit
ordering, handler PC, and return to post-TRAPA address.

---

## Task 13 — Implement real exception entry and RTE using VBR and stack

Upgrade `request_exception_vector(v)` from the synthetic scaffold to real SH-2 semantics:
1. `R15 -= 4; write(SR, @R15)` — push SR
2. `R15 -= 4; write(PC, @R15)` — push current PC (return address)
3. `PC = read(VBR + (v * 4))` — fetch handler address; set I-bits in SR to mask level

Upgrade `RTE` (`0x002B`) from synthetic scaffold to real stack-based restore:
1. `PC = read(@R15); R15 += 4` — pop PC
2. `SR = read(@R15); R15 += 4` — pop SR
3. Execute delay slot before applying the restored PC

Emit `EXCEPTION_ENTRY` and `EXCEPTION_RETURN` trace labels (distinct from the old
`SYNTHETIC_EXCEPTION_ENTRY` / `SYNTHETIC_RTE` labels, which should remain recognized for
backward compatibility with existing tests but not generated by new code paths).

Add a deterministic regression: trigger an exception via `request_exception_vector`, confirm
the stack commit sequence (SR push then PC push), confirm handler PC is fetched from VBR
table, execute RTE, and assert PC and SR are restored to pre-exception values.

---

## Task 14 — Implement @(disp,Rn) displacement addressing for loads and stores

Add the register+displacement addressing forms:
- `MOV.L @(disp,Rn),Rm` = `0x5nmF` — 4-bit disp scaled by 4; EA = Rn + (disp * 4)
- `MOV.L Rm,@(disp,Rn)` = `0x1nmF` — 4-bit disp scaled by 4; EA = Rn + (disp * 4)
- `MOV.W @(disp,Rn),R0` = `0x85nd` — 4-bit disp scaled by 2; dest always R0; sign-extend
- `MOV.B @(disp,Rn),R0` = `0x84nd` — 4-bit disp unscaled; dest always R0; sign-extend
- `MOV.W R0,@(disp,Rn)` = `0x81nd` — 4-bit disp scaled by 2; source always R0
- `MOV.B R0,@(disp,Rn)` = `0x80nd` — 4-bit disp unscaled; source always R0

Each emits a blocking bus op through the arbiter. Add a deterministic regression covering read
and write forms at disp=0 and disp>0, sign extension on the word and byte read forms (both
positive and negative values), and a round-trip write+read at displacement.

---

## Task 15 — Implement MUL.L and add MACH/MACL registers

Add `mach_` and `macl_` (32-bit each) to `SH2Core`, initialized to 0 on `reset()`.

Implement with correct SH-2 manual semantics:
- `MUL.L Rm,Rn` = `0000nnnnmmmm0111` = `0x0nm7`
  Operation: `Rn × Rm → MACL` (32×32 signed multiply, **32-bit result only, stored in MACL**)
  MACH is NOT written by MUL.L.

Also implement register-transfer instructions:
- `STS MACL,Rn` = `0x0n1A` — copy MACL to Rn
- `STS MACH,Rn` = `0x000A` (verify: `0x0n0A`) — copy MACH to Rn
- `LDS Rm,MACL` = `0x4m1A` — copy Rm to MACL
- `LDS Rm,MACH` = `0x4m0A` — copy Rm to MACH

Note: for 64-bit multiply results, those are `DMULS.L` / `DMULU.L` — separate instructions,
not in this task.

Model MUL.L as 2 ticks of local time per the vertical-slice convention.

Add a deterministic regression: multiply two known 32-bit values, read MACL via STS, assert
the low-32-bit product. Include a case that confirms MACH is not clobbered.

---

## Task 16 — Refresh docs, run full regression, and establish BIOS forward-progress baseline

1. Update `docs/architecture.md` to document: VBR/GBR/MACH/MACL registers, PC-relative
   addressing base rule, real exception entry/RTE semantics, expanded addressing modes, and
   the pre-decrement addressing forms.

2. Update `docs/todo.md` to mark Tasks 1–15 complete and populate a new next-batch section
   covering: `BSR` (branch-to-subroutine), `JMP @Rm`, `@(R0,Rn)` indexed addressing,
   `DMULS.L`/`DMULU.L`, `MULS.W`/`MULU.W`, `DIV0U`/`DIV1`, `EXTS.B`/`EXTS.W`/`EXTU.B`/
   `EXTU.W`, `@(disp,GBR)` addressing, `SHLL2`/`SHLL8`/`SHLL16` (fast shifts), and `NEG`.

3. Run the full test suite and confirm all existing determinism invariants (ST/MT trace parity,
   commit-prefix stability, SCU/BIOS fixture stability) remain green.

4. Run BIOS bring-up mode (`--bios /path/to/bios.bin --headless --trace bios_progress.jsonl`)
   and record: (a) how many instructions execute before the first `ILLEGAL_OP` fault, (b) the
   opcode of that first fault. Commit this instruction count as a numeric regression baseline
   fixture so future batches can track BIOS forward progress quantitatively. This baseline
   replaces qualitative "BIOS bring-up is partial" status with a measurable number.
