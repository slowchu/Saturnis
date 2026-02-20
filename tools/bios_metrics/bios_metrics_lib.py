#!/usr/bin/env python3
"""Helpers for local BIOS ILLEGAL_OP forward-progress instrumentation."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class IllegalOpEvent:
    pc: int
    opcode: int


def _parse_fault_payload(line: str) -> dict:
    prefix = "FAULT "
    if not line.startswith(prefix):
        raise ValueError("not a fault line")
    return json.loads(line[len(prefix) :])


def extract_illegal_ops(trace_lines: Iterable[str]) -> list[IllegalOpEvent]:
    events: list[IllegalOpEvent] = []
    for raw in trace_lines:
        line = raw.strip()
        if not line.startswith("FAULT "):
            continue
        payload = _parse_fault_payload(line)
        if payload.get("reason") != "ILLEGAL_OP":
            continue
        events.append(IllegalOpEvent(pc=int(payload.get("pc", 0)), opcode=int(payload.get("detail", 0))))
    return events


def build_metrics(illegal_ops: list[IllegalOpEvent]) -> dict:
    counts = Counter(op.opcode for op in illegal_ops)
    first = illegal_ops[0] if illegal_ops else IllegalOpEvent(pc=0, opcode=0)
    return {
        "format_version": 1,
        "illegal_op_count": len(illegal_ops),
        "first_illegal_op": {
            "opcode": first.opcode,
            "pc": first.pc,
        },
        "top_illegal_opcodes": [
            {"opcode": opcode, "count": count}
            for opcode, count in sorted(counts.items(), key=lambda item: (-item[1], item[0]))
        ],
    }


def parse_trace_file(path: Path) -> dict:
    lines = path.read_text(encoding="utf-8").splitlines()
    illegal_ops = extract_illegal_ops(lines)
    return build_metrics(illegal_ops)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_top_report(path: Path, metrics: dict, top_n: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# BIOS unsupported-opcode summary (local only)",
        "",
        f"- illegal_op_count: {metrics['illegal_op_count']}",
        f"- first_illegal_opcode: {metrics['first_illegal_op']['opcode']}",
        f"- first_illegal_pc: {metrics['first_illegal_op']['pc']}",
        "",
        "## Top unsupported opcodes",
        "",
        "| rank | opcode | count |",
        "| ---: | -----: | ----: |",
    ]
    for idx, row in enumerate(metrics["top_illegal_opcodes"][:top_n], start=1):
        lines.append(f"| {idx} | {row['opcode']} | {row['count']} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _main() -> int:
    parser = argparse.ArgumentParser(description="Parse a Saturnis trace and emit ILLEGAL_OP metrics JSON.")
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--top-report", type=Path)
    parser.add_argument("--top-n", type=int, default=10)
    args = parser.parse_args()

    metrics = parse_trace_file(args.trace)
    write_json(args.out, metrics)
    if args.top_report is not None:
        write_top_report(args.top_report, metrics, args.top_n)
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
