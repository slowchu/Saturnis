# BIOS Forward-Progress Instrumentation (Local Only)

This workflow is intentionally **local-only**. BIOS/ROM assets must not be committed, uploaded, or used by CI.

## Purpose

Track BIOS forward progress by measuring `ILLEGAL_OP` faults in local trace runs.

## Artifact format

Baseline/latest metrics are JSON files with this schema:

```json
{
  "format_version": 1,
  "illegal_op_count": 123,
  "first_illegal_op": {
    "opcode": 4660,
    "pc": 4096
  },
  "top_illegal_opcodes": [
    { "opcode": 4660, "count": 10 },
    { "opcode": 65535, "count": 6 }
  ]
}
```

Minimum baseline comparison keys are:
- `illegal_op_count`
- `first_illegal_op.opcode`
- `first_illegal_op.pc`

## Local commands

1. Build emulator first:

```bash
cmake -S . -B build
cmake --build build
```

2. Run local BIOS trace + metrics capture:

```bash
python3 tools/bios_metrics/run_bios_illegal_metrics.py \
  --emu ./build/saturnemu \
  --bios /path/to/local/bios.bin \
  --max-steps 2000000 \
  --trace-out local/bios_trace.jsonl \
  --metrics-out local/bios_illegal_metrics.json \
  --report-out local/bios_illegal_report.md
```

3. Compare latest metrics against a local baseline:

```bash
python3 tools/bios_metrics/diff_bios_illegal_metrics.py \
  --baseline local/bios_illegal_metrics_baseline.json \
  --latest local/bios_illegal_metrics.json
```

`diff_bios_illegal_metrics.py` exits non-zero only when `illegal_op_count` regresses.

## Deterministic parser tests

Fixture-driven parser tests run in CI via CTest (`saturnis_bios_metrics_scripts`) and validate:
- trace parsing,
- stable metrics emission,
- deterministic diff behavior.

Fixtures live under `tests/fixtures/bios_metrics/`.
