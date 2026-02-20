#!/usr/bin/env python3
"""Compare local BIOS ILLEGAL_OP metrics artifacts against a baseline."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Diff baseline and latest BIOS ILLEGAL_OP metrics.")
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--latest", required=True, type=Path)
    args = parser.parse_args()

    base = load(args.baseline)
    latest = load(args.latest)

    base_count = int(base.get("illegal_op_count", 0))
    latest_count = int(latest.get("illegal_op_count", 0))
    base_first = base.get("first_illegal_op", {"opcode": 0, "pc": 0})
    latest_first = latest.get("first_illegal_op", {"opcode": 0, "pc": 0})

    print(f"baseline.count={base_count}")
    print(f"latest.count={latest_count}")
    print(f"delta.count={latest_count - base_count}")
    print(f"baseline.first_opcode={int(base_first.get('opcode', 0))}")
    print(f"latest.first_opcode={int(latest_first.get('opcode', 0))}")
    print(f"baseline.first_pc={int(base_first.get('pc', 0))}")
    print(f"latest.first_pc={int(latest_first.get('pc', 0))}")

    # Non-zero return only for count regressions; opcode/pc changes are informational.
    if latest_count > base_count:
        print("result=REGRESSION")
        return 1
    print("result=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
