Saturnis → Ymir Roadmap (Corrected)
North Star (Rewritten)
Primary Deliverable (Ymir-facing)

A small, reusable, deterministic libbusarb library that Ymir can call from its existing bus-wait hook to answer:

“Should this bus master stall right now, and if so for how long?”

Explicitly Not the Deliverable (for now)

A full SH-2 executor

Full ISA decode/dispatch refactor

MA/IF pipeline-stage modeling inside Saturnis

A running Saturn emulator

Those are valid future goals, but they are not on the critical path to helping Ymir.

Guiding Principle

Separate “timing/arbitration infrastructure” from “CPU execution correctness.”

Ymir already has CPU execution.
What it lacks (and what Saturnis can contribute) is a deterministic arbitration/timing mechanism with a testable integration seam.

Roadmap Structure
Track A (P0/P1): Ymir Integration Deliverable

This is the only track that should be considered active / priority right now.

Track B (P2+): Deeper Hardware Modeling (MA/IF, stage-aware)

This depends on Striker’s answer and Ymir-side instrumentation.

Track C (Optional Future): Saturnis as Full Emulator

This is explicitly decoupled and should not block Track A.

Track A — P0/P1 (Active): libbusarb Extraction + Ymir Seam
Gate A1 — Extract libbusarb as a standalone static library
Goal

Turn Saturnis’s arbiter into a small reusable library with no Saturnis-specific memory/router dependencies.

Deliverables

New build target: busarb / libbusarb (static lib)

Public headers under include/busarb/...

No dependency on Saturnis device router / memory implementation

Minimal public API surface

Acceptance Criteria

Builds independently in Saturnis repo

Unit tests run without BIOS/ROM/device graph

Public headers do not include Saturnis internal types

Gate A2 — Define stable callback interface (timing source abstraction)
Goal

Let Ymir supply timing (GetAccessCycles) without Saturnis duplicating Ymir’s timing tables.

Required API (minimal)
struct TimingCallbacks {
  uint32_t (*access_cycles)(void* ctx, uint32_t addr, bool is_write, uint8_t size_bytes);
  void* ctx;
};

(Or equivalent C++ interface / functor-based form)

Notes

Keep this plain C++ (no Ymir types)

size_bytes included even if Ymir currently ignores/normalizes it

Optional is_mmio(addr) only if you prove it’s needed

Acceptance Criteria

libbusarb compiles and runs using a mock callback in tests

No Saturnis latency model required for core library function

Gate A3 — Stable request/response types (public POD API)
Goal

Expose a narrow, durable interface for Ymir to call.

Required Types

BusMasterId (enum or integer)

minimum: SH2_A, SH2_B, DMA

extensible

BusRequest

master_id

addr

is_write

size_bytes

now_tick

BusWaitResult

should_wait

wait_cycles (stall only)

Important Semantics

now_tick is treated as an opaque monotonic tick from the caller’s timebase

wait_cycles means stall time until request may begin, not total service duration

Acceptance Criteria

Public API documented in header comments

No hidden dependency on Saturnis internal tick units

Gate A4 — Replace one-call “oracle” with a query/commit API (critical fix)

This is the biggest roadmap correction.

Why this is required

A single mutating bus_wait(req) cannot reliably be both:

call-order independent, and

stateful
when two masters contend at the same tick.

Correct P0 API shape

Use two-phase semantics:

1) Query (non-mutating)
BusWaitResult query_wait(const BusRequest& req) const;
2) Commit (mutating)
void commit_grant(const BusRequest& req, uint32_t tick_start);

commit_grant() computes service duration via access_cycles(...) and updates bus/master availability.

Tie-break policy (P0)

Keep it simple and deterministic:

fixed master priority (documented), e.g. DMA > SH2_A > SH2_B

no round-robin in P0 unless Striker specifically asks for fairness behavior

Why this solves your earlier issue

query_wait() can be call-order independent because it does not mutate state

Ymir can query multiple contenders and then commit the winner

deterministic arbitration is preserved

Acceptance Criteria

Same-tick contention queries return consistent results regardless of query order

State changes happen only through commit_grant()

Gate A5 — Minimal trace hooks (optional but strongly recommended)
Goal

Make arbitration decisions observable and replayable.

API (optional callbacks)
on_grant(master_id, addr, is_write, size_bytes, tick_start, tick_end)

No-op if unset.

Why it matters

Helps Striker debug integration quickly

Lets you compare behavior across runs

Creates the seed for a replay harness later

Acceptance Criteria

Tracing can be enabled in tests without changing library logic

Zero-overhead/no-op path when disabled (or negligible)

Gate A6 — Unit tests for the seam (must-have)
Required tests

Determinism

identical request/commit sequence => identical outputs/state

Query call-order independence

same-tick contenders queried A→B vs B→A

query results must not depend on order

Commit determinism

same winner committed => same follow-up waits

Basic timing

busy bus returns expected wait_cycles

duration matches access_cycles(...) callback

Tie-break policy

same-tick conflict resolves per documented priority, not caller order

Acceptance Criteria

No BIOS/ROM required

Small fast tests suitable for CI

Gate A7 — Minimal Ymir integration notes (handoff-ready)
Goal

Make adoption trivial for Striker.

README section should include

What libbusarb does (stall oracle; not full bus simulator)

Required callback (access_cycles)

How to map Ymir FnBusWait / IsBusWait to query_wait()

When to call commit_grant()

Current limitations (no MA/IF stage modeling yet)

Acceptance Criteria

Striker can wire a prototype adapter without reading Saturnis internals

Track B — P2 (Depends on Striker / Ymir-side changes): MA/IF Contention Modeling

This is where the roadmap was previously too early and too broad.

Gate B1 — Clarify requirement with Striker (required before implementation)

You still need the answer to:

For MA/IF contention, do you require explicit IF/MA arbitration modeling, or is correct stall timing sufficient?

Without this answer, you risk overbuilding.

Acceptance Criteria

Written response from Striker (issue comment / DM / repo note)

Requirement recorded in roadmap/doc

Gate B2 — Define Ymir-side instrumentation contract

libbusarb alone cannot invent MA/IF contention if Ymir does not expose enough info.

Minimum metadata (if doing timing-based MA/IF)

Ymir likely needs to annotate requests or internal SH-2 timing with:

ma_cycles

if_cycles

ifetch_hits_bus / is_ifetch_cache_hit (or equivalent)

stage timing basis (now_tick semantics)

Acceptance Criteria

Agreed metadata schema between Saturnis/Ymir

Confirmed source location in Ymir (SH-2 core vs bus layer)

Gate B3 — Implement MA/IF policy (only after B1/B2)

Two possible branches:

B3a) Outcome model (stall timing only)

Use manual-derived rules:

split vs no-split

MA priority

MA + IF state accounting

if no-bus-cycle special case

B3b) Mechanism model (explicit IF/MA contenders)

Represent IF and MA as separate internal requests (heavier)

Acceptance Criteria

Matches agreed requirement from B1

Covered by dedicated tests (manual example sequences)

Track C — Optional Future (Not Blocking Ymir): Saturnis Full Emulator Build-Out

This is where decode/dispatch work belongs.

Gate C1 — Full decode family coverage (optional)

Useful for Saturnis itself, trace quality, and future CPU work.
Not required for Ymir libbusarb handoff.

Gate C2 — Opcode enum + dispatch table (optional)

Normal emulator architecture step.
Not a Ymir deliverable.

Gate C3 — SH-2 execution refactor (optional)

Replace if/else chain, etc.
Explicitly out of scope for current handoff.

What to Freeze Right Now (Important)
Freeze (until Track A lands)

Full SH-2 decode expansion (unless directly required for a future MA/IF metadata prototype)

OpcodeType enum / 65536 dispatch table

sh2_core.cpp dispatch refactor

Any attempt to build Saturnis into a running emulator as part of Ymir deliverable work

This is how you stop roadmap drift.

Rewritten Priority Order (Concrete)
Immediate (next work)

libbusarb extraction (A1)

callback interface (A2)

public request/response PODs (A3)

query/commit API (A4) ← critical design correction

unit tests (A6)

README integration notes (A7)

optional trace hook (A5)

After Striker tests a prototype

integration feedback fixes (API polish, tie-break tweaks)

decide whether MA/IF is P2a (timing rules) or P2b (explicit internal contention)

only then consider deeper Ymir SH-2 instrumentation
