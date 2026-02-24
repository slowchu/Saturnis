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
                "<QQQQQI4B",
                rec["seq"],
                rec["tick_first_attempt"],
                rec["tick_complete"],
                rec["service_cycles"],
                rec["retries"],
                addr,
                rec["size"],
                _encode_master(rec["master"]),
                rw,
                _encode_kind(rec["kind"]),
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
        "trace_observed",
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
        "cache_bucket_distribution",
        "master_region_access_kind_cache_bucket_distribution",
        "observed_bucket_stats_by_master_region_access_kind_cache_bucket",
        "symmetry_checks",
        "delta_histogram",
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

    if data["trace_observed"]["source"] != "TRACE_ONLY":
        print("expected trace_observed.source to be TRACE_ONLY")
        return 1

    if not data["observed_bucket_stats_by_master_region_access_kind_cache_bucket"]:
        print("expected observed bucket stats map to be non-empty")
        return 1
    first_bucket_stats = next(iter(data["observed_bucket_stats_by_master_region_access_kind_cache_bucket"].values()))
    for stat_field in ["sample_size", "observed_elapsed_p90", "observed_wait_nonzero_rate", "low_sample"]:
        if stat_field not in first_bucket_stats:
            print(f"missing observed bucket stat field: {stat_field}")
            return 1

    if not isinstance(data["symmetry_checks"], list):
        print("expected symmetry_checks to be a list")
        return 1


    annotated_lines = [ln for ln in annotated.read_text().splitlines() if ln.strip()]
    if len(annotated_lines) != 10:
        print(f"unexpected annotated line count: {len(annotated_lines)}")
        return 1

    parsed_first = json.loads(annotated_lines[0])
    expected_elapsed = parsed_first["tick_complete"] - parsed_first["tick_first_attempt"]
    if parsed_first["observed_elapsed"] != expected_elapsed:
        print(
            f"unexpected observed_elapsed: {parsed_first['observed_elapsed']} != {expected_elapsed} (exclusive tick contract)"
        )
        return 1

    for field in [
        "observed_service_cycles",
        "observed_retries",
        "observed_elapsed",
        "observed_wait",
        "cache_bucket",
        "classification",
    ]:
        if field not in parsed_first:
            print(f"missing annotated field: {field}")
            return 1

    if "model_vs_trace_wait_delta" in parsed_first:
        print("did not expect model fields without --include-model-comparison")
        return 1

    model_summary = build_dir / "trace_replay_tool_summary_model.json"
    proc_model = subprocess.run(
        [
            str(trace_replay),
            str(fixture),
            "--summary-output",
            str(model_summary),
            "--summary-only",
            "--include-model-comparison",
            "--top-k",
            "5",
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc_model.returncode != 0:
        print(proc_model.stdout)
        print(proc_model.stderr)
        return 1
    model_data = json.loads(model_summary.read_text())
    if "model_comparison" not in model_data:
        print("expected model_comparison section with --include-model-comparison")
        return 1
    for model_key in [
        "hypothesis_mismatch_count",
        "model_vs_trace_wait_delta_by_access_kind",
        "top_model_vs_trace_wait_deltas",
    ]:
        if model_key not in model_data["model_comparison"]:
            print(f"expected model key with --include-model-comparison: {model_key}")
            return 1

    model_annotated = build_dir / "trace_replay_tool_annotated_model.jsonl"
    proc_model_annotated = subprocess.run(
        [
            str(trace_replay),
            str(fixture),
            "--annotated-output",
            str(model_annotated),
            "--include-model-comparison",
            "--top-k",
            "5",
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc_model_annotated.returncode != 0:
        print(proc_model_annotated.stdout)
        print(proc_model_annotated.stderr)
        return 1
    model_first = json.loads(model_annotated.read_text().splitlines()[0])
    for model_field in [
        "model_predicted_wait",
        "model_predicted_service",
        "model_predicted_total",
        "model_vs_trace_wait_delta",
        "model_vs_trace_total_delta",
    ]:
        if model_field not in model_first:
            print(f"missing model annotated field: {model_field}")
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
