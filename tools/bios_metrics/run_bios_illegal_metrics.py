#!/usr/bin/env python3
"""Run saturnemu locally with BIOS and capture ILLEGAL_OP metrics/report artifacts."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

from bios_metrics_lib import parse_trace_file, write_json, write_top_report


def main() -> int:
    parser = argparse.ArgumentParser(description="Local-only BIOS forward-progress metric runner.")
    parser.add_argument("--emu", type=Path, default=Path("./build/saturnemu"))
    parser.add_argument("--bios", required=True, type=Path)
    parser.add_argument("--max-steps", type=int, default=2_000_000)
    parser.add_argument("--trace-out", type=Path, default=Path("local/bios_trace.jsonl"))
    parser.add_argument("--metrics-out", type=Path, default=Path("local/bios_illegal_metrics.json"))
    parser.add_argument("--report-out", type=Path, default=Path("local/bios_illegal_report.md"))
    parser.add_argument("--top-n", type=int, default=20)
    args = parser.parse_args()

    args.trace_out.parent.mkdir(parents=True, exist_ok=True)
    args.metrics_out.parent.mkdir(parents=True, exist_ok=True)
    args.report_out.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(args.emu),
        "--bios",
        str(args.bios),
        "--headless",
        "--trace",
        str(args.trace_out),
        "--max-steps",
        str(args.max_steps),
    ]
    subprocess.run(cmd, check=True)

    metrics = parse_trace_file(args.trace_out)
    write_json(args.metrics_out, metrics)
    write_top_report(args.report_out, metrics, args.top_n)

    print(f"wrote trace:   {args.trace_out}")
    print(f"wrote metrics: {args.metrics_out}")
    print(f"wrote report:  {args.report_out}")
    print(f"illegal_op_count={metrics['illegal_op_count']}")
    print(f"first_illegal_opcode={metrics['first_illegal_op']['opcode']}")
    print(f"first_illegal_pc={metrics['first_illegal_op']['pc']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
