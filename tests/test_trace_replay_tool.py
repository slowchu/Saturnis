#!/usr/bin/env python3
import json
import pathlib
import struct
import subprocess
import sys


def _encode_kind(kind: str) -> int:
    table = {"ifetch": 0, "read": 1, "write": 2, "mmio_read": 3, "mmio_write": 4}
    return table[kind]


def _encode_master(master: str) -> int:
    table = {"MSH2": 0, "SSH2": 1, "DMA": 2}
    return table[master]


def _write_binary_fixture_from_jsonl(src: pathlib.Path, dst: pathlib.Path) -> None:
    records = []
    for line in src.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("MALFORMED"):
            continue
        records.append(json.loads(line))

    with dst.open("wb") as f:
        f.write(b"BTR1" + struct.pack("<HH", 1, 48))
        for rec in records:
            addr = int(rec["addr"], 16)
            rw = 1 if rec["rw"] == "W" else 0
            packed = struct.pack(
                "<QQQIIIBBBBII",
                rec["seq"],
                rec["tick_first_attempt"],
                rec["tick_complete"],
                addr,
                rec["service_cycles"],
                rec["retries"],
                _encode_master(rec["master"]),
                rw,
                rec["size"],
                _encode_kind(rec["kind"]),
                0,
                0,
            )
            assert len(packed) == 48
            f.write(packed)



def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    build_dir = root / "build"
    trace_replay = build_dir / "trace_replay"
    fixture = root / "tests" / "fixtures" / "trace_replay" / "sample_trace.jsonl"
    annotated = build_dir / "trace_replay_tool_annotated.jsonl"
    summary = build_dir / "trace_replay_tool_summary.json"

    proc = subprocess.run(
        [
            str(trace_replay),
            str(fixture),
            "--annotated-output",
            str(annotated),
            "--summary-output",
            str(summary),
            "--top-k",
            "5",
        ],
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
        "summary_schema_version",
        "records_processed",
        "malformed_lines_skipped",
        "duplicate_seq_count",
        "non_monotonic_seq_count",
        "total_events",
        "included_events",
        "excluded_events",
        "included_master_distribution",
        "included_region_distribution",
        "included_size_distribution",
        "included_rw_distribution",
        "included_access_kind_distribution",
        "included_master_region_distribution",
        "agreement_count",
        "mismatch_count",
        "known_gap_count",
        "known_gap_byte_access_count",
        "normalized_agreement_count",
        "normalized_mismatch_count",
        "mean_base_latency",
        "mean_contention_stall",
        "mean_total_predicted",
        "normalized_mismatch_by_master_region_access_kind",
        "normalized_delta_by_access_kind",
        "delta_histogram",
        "top_cumulative_drifts",
        "top_normalized_deltas",
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
    if data["duplicate_seq_count"] != 0:
        print(f"unexpected duplicate_seq_count: {data['duplicate_seq_count']}")
        return 1
    if data["non_monotonic_seq_count"] == 0:
        print("expected non_monotonic_seq_count to be non-zero for fixture")
        return 1

    if data["included_events"] + data["excluded_events"] != data["total_events"]:
        print("dataset hygiene invariant failed: included + excluded != total")
        return 1

    if "ifetch" not in data["normalized_delta_by_access_kind"] and "read" not in data["normalized_delta_by_access_kind"]:
        print("expected normalized_delta_by_access_kind to contain at least one access kind bucket")
        return 1


    if data["normalized_agreement_count"] == 0:
        print("expected normalized_agreement_count to be non-zero for fixture")
        return 1


    annotated_lines = [ln for ln in annotated.read_text().splitlines() if ln.strip()]
    if len(annotated_lines) != 10:
        print(f"unexpected annotated line count: {len(annotated_lines)}")
        return 1

    parsed_first = json.loads(annotated_lines[0])
    expected_elapsed = parsed_first["tick_complete"] - parsed_first["tick_first_attempt"]
    if parsed_first["ymir_elapsed"] != expected_elapsed:
        print(
            f"unexpected ymir_elapsed: {parsed_first['ymir_elapsed']} != {expected_elapsed} (exclusive tick contract)"
        )
        return 1

    for field in [
        "ymir_service_cycles",
        "ymir_retries",
        "ymir_elapsed",
        "ymir_wait",
        "arbiter_predicted_wait",
        "arbiter_predicted_service",
        "arbiter_predicted_total",
        "base_latency",
        "contention_stall",
        "total_predicted",
        "normalized_delta_wait",
        "cumulative_drift_total",
        "classification",
    ]:
        if field not in parsed_first:
            print(f"missing annotated field: {field}")
            return 1


    binary_fixture = build_dir / "trace_replay_tool_fixture.bin"
    binary_summary = build_dir / "trace_replay_tool_summary_bin.json"
    _write_binary_fixture_from_jsonl(fixture, binary_fixture)

    proc_bin = subprocess.run(
        [
            str(trace_replay),
            str(binary_fixture),
            "--summary-output",
            str(binary_summary),
            "--summary-only",
            "--top-k",
            "5",
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc_bin.returncode != 0:
        print(proc_bin.stdout)
        print(proc_bin.stderr)
        return 1

    bin_data = json.loads(binary_summary.read_text())
    if bin_data["records_processed"] != 10:
        print(f"unexpected binary records_processed: {bin_data['records_processed']}")
        return 1
    if bin_data["malformed_lines_skipped"] != 0:
        print(f"unexpected binary malformed_lines_skipped: {bin_data['malformed_lines_skipped']}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
