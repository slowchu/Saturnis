#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    build_dir = root / "build"
    trace_replay = build_dir / "trace_replay"
    fixture = root / "tests" / "fixtures" / "trace_replay" / "sample_trace.jsonl"
    annotated = build_dir / "trace_replay_tool_annotated.jsonl"
    summary = build_dir / "trace_replay_tool_summary.json"

    proc = subprocess.run(
        [str(trace_replay), str(fixture), "--annotated-output", str(annotated), "--summary-output", str(summary), "--top", "5"],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
      print(proc.stdout)
      print(proc.stderr)
      return 1

    data = json.loads(summary.read_text())
    required_keys = {
        "records_processed",
        "malformed_lines_skipped",
        "agreement_count",
        "mismatch_count",
        "known_gap_count",
        "known_gap_byte_access_count",
        "delta_histogram",
        "top_deltas",
    }
    missing = required_keys.difference(data.keys())
    if missing:
        print(f"missing summary keys: {sorted(missing)}")
        return 1

    if data["records_processed"] != 10:
        print(f"unexpected records_processed: {data['records_processed']}")
        return 1
    if data["malformed_lines_skipped"] != 1:
        print(f"unexpected malformed_lines_skipped: {data['malformed_lines_skipped']}")
        return 1

    annotated_lines = [ln for ln in annotated.read_text().splitlines() if ln.strip()]
    if len(annotated_lines) != 10:
        print(f"unexpected annotated line count: {len(annotated_lines)}")
        return 1

    parsed_first = json.loads(annotated_lines[0])
    for field in [
        "ymir_service_cycles",
        "ymir_retries",
        "ymir_effective_wait",
        "ymir_effective_total",
        "arbiter_wait",
        "arbiter_service_cycles",
        "arbiter_total",
        "classification",
    ]:
        if field not in parsed_first:
            print(f"missing annotated field: {field}")
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
