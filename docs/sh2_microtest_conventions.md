# SH-2 Microtest Conventions

This document defines the naming and structure conventions for SH-2 microtests.

## Naming convention

Use this shape for new tests:

- `test_p<priority>_sh2_<cluster>_<behavior>_<expectation>()`

Examples:
- `test_p2_sh2_alu_addc_carry_chain_sets_t_bit()`
- `test_p1_sh2_mem_post_increment_alias_m_eq_n_is_stable()`

Guidelines:
- `priority`: `p0` (critical), `p1` (roadmap implementation), `p2` (infrastructure/hardening).
- `cluster`: `alu`, `branch`, `mem`, `exception`, `decode`, `system`.
- `behavior` + `expectation`: short, explicit, deterministic language.

## Template

```cpp
void test_p2_sh2_<cluster>_<behavior>_<expectation>() {
  saturnis::core::TraceLog trace;
  saturnis::mem::CommittedMemory mem;
  saturnis::dev::DeviceHub dev;
  saturnis::bus::BusArbiter arbiter(mem, dev, trace);

  // Arrange deterministic fixture program/data.
  mem.write(0x0000U, 2U, 0x0009U); // NOP

  saturnis::cpu::SH2Core core(0);
  core.reset(0U, 0x0001FFF0U);

  for (std::uint64_t i = 0U; i < 4U; ++i) {
    core.step(arbiter, trace, i);
  }

  check(/* deterministic predicate */, "<clear deterministic assertion message>");
}
```

## Determinism checklist

- No wall-clock or random dependencies.
- Assertions use explicit architectural state (`PC/SR/GPRs/memory/trace`) and stable messages.
- For multi-step fixtures, prefer checkpoint-style assertions at fixed step indices.
