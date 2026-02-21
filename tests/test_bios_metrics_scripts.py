#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools" / "bios_metrics"
FIXTURES = ROOT / "tests" / "fixtures" / "bios_metrics"


class BiosMetricsScriptsTest(unittest.TestCase):
    def test_parser_metrics_match_fixture_baseline(self) -> None:
        trace = FIXTURES / "sample_trace.jsonl"
        baseline = json.loads((FIXTURES / "baseline_metrics.json").read_text(encoding="utf-8"))

        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "metrics.json"
            subprocess.run(
                [
                    "python3",
                    str(TOOLS / "bios_metrics_lib.py"),
                    "--trace",
                    str(trace),
                    "--out",
                    str(out),
                ],
                check=True,
                cwd=ROOT,
            )
            got = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(got, baseline)

    def test_diff_script_regression_and_non_regression(self) -> None:
        baseline = FIXTURES / "baseline_metrics.json"
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            same = td_path / "same.json"
            same.write_text(baseline.read_text(encoding="utf-8"), encoding="utf-8")
            ok = subprocess.run(
                [
                    "python3",
                    str(TOOLS / "diff_bios_illegal_metrics.py"),
                    "--baseline",
                    str(baseline),
                    "--latest",
                    str(same),
                ],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(ok.returncode, 0)
            self.assertIn("result=OK", ok.stdout)

            regression = td_path / "regression.json"
            reg_payload = json.loads(baseline.read_text(encoding="utf-8"))
            reg_payload["illegal_op_count"] = int(reg_payload["illegal_op_count"]) + 1
            regression.write_text(json.dumps(reg_payload), encoding="utf-8")
            bad = subprocess.run(
                [
                    "python3",
                    str(TOOLS / "diff_bios_illegal_metrics.py"),
                    "--baseline",
                    str(baseline),
                    "--latest",
                    str(regression),
                ],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(bad.returncode, 1)
            self.assertIn("result=REGRESSION", bad.stdout)


if __name__ == "__main__":
    unittest.main()
