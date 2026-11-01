#!/usr/bin/env python3
"""Self-test of the R4 evaluation (r4_driver.summarize, report; r4_report): synthetic records with known answers.
Usage: python3 -B plugins/spike/r4_selftest.py
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import r4_driver  # noqa: E402


def record(index: int, total: float, kind: str = "measured") -> dict:
    build, reload = total * 0.9, total * 0.08
    load = {"loadavg": ["0.1", "0.1", "0.1"], "psi_cpu_some_avg10": 0.0, "psi_memory_some_avg10": 0.0}
    return {"index": index, "kind": kind, "ok": True, "build": build, "reload": reload,
            "answer": total - build - reload, "total": total, "load_before": load, "load_after": load}


def probe(index: int, kind: str, seconds: float) -> dict:
    load = {"loadavg": ["0.1", "0.1", "0.1"], "psi_cpu_some_avg10": 0.0, "psi_memory_some_avg10": 0.0}
    return {"index": index, "kind": kind, "ok": True, "seconds": seconds, "load_before": load, "load_after": load}


class Summaries(unittest.TestCase):
    def test_medians_exclude_warmups_and_probes(self) -> None:
        records = [record(0, 50.0, "warmup")] + [record(i, t) for i, t in enumerate((4.0, 6.0, 5.0), 1)] + \
            [probe(0, "rebuild_unchanged", 0.3), probe(1, "rebuild_unchanged", 0.5), probe(0, "reload_unchanged", 0.1)]
        summary = r4_driver.summarize(records)
        self.assertEqual(summary["total"], {"median": 5.0, "min": 4.0, "max": 6.0})
        self.assertEqual(summary["rebuild_unchanged"]["median"], 0.4)
        self.assertEqual(summary["reload_unchanged"]["max"], 0.1)

    def test_decision_rule_at_its_boundaries(self) -> None:
        def verdict(*totals: float) -> str:
            return r4_driver.summarize([record(i, t) for i, t in enumerate(totals)])["verdict"]
        self.assertEqual(verdict(4.0, 9.99, 5.0), "met")
        self.assertEqual(verdict(4.0, 10.0, 5.0), "median_met")  # one total at 10 s is not under 10 s
        self.assertEqual(verdict(4.0, 12.0, 5.0), "median_met")
        self.assertEqual(verdict(10.0, 12.0, 4.0), "not_met")    # median 10 s is not under 10 s
        self.assertEqual(verdict(11.0, 12.0, 13.0), "not_met")

    def test_no_measured_sample_has_no_verdict(self) -> None:
        self.assertNotIn("verdict", r4_driver.summarize([record(0, 5.0, "warmup")]))


class Reports(unittest.TestCase):
    def test_report_shows_the_verdict_only_for_a_valid_run(self) -> None:
        records = [record(i, 5.0) for i in range(3)]
        summary = r4_driver.summarize(records)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            r4_driver.report(records, summary, out, valid=True)
            self.assertIn("Verdict: met", (out / "report.html").read_text())
            r4_driver.report(records, summary, out, valid=False)
            page = (out / "report.html").read_text()
            self.assertIn("UNEVALUATED", page)
            self.assertNotIn("Verdict: met", page)

    def test_combined_runs_pool_measured_samples_and_any_failed_run_invalidates(self) -> None:
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, totals, failed in (("run1", (4.0, 6.0), []), ("run2", (5.0, 7.0), [])):
                run = root / name
                run.mkdir()
                lines = [record(0, 40.0, "warmup")] + [record(i, t) for i, t in enumerate(totals, 1)]
                (run / "samples.jsonl").write_text("".join(json.dumps(r) + "\n" for r in lines))
                (run / "summary.json").write_text(json.dumps({"failed": failed}))
            script = Path(__file__).resolve().parent / "r4_report.py"
            ok = subprocess.run([sys.executable, "-B", str(script), str(root / "a"), str(root / "run1"), str(root / "run2")],
                                capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            summary = json.loads((root / "a" / "summary.json").read_text())
            self.assertTrue(summary["valid"])
            self.assertEqual(summary["summary"]["total"], {"median": 5.5, "min": 4.0, "max": 7.0})
            (root / "run2" / "summary.json").write_text(json.dumps({"failed": ["validity: something"]}))
            bad = subprocess.run([sys.executable, "-B", str(script), str(root / "b"), str(root / "run1"), str(root / "run2")],
                                 capture_output=True, text=True)
            self.assertEqual(bad.returncode, 1)
            self.assertFalse(json.loads((root / "b" / "summary.json").read_text())["valid"])
            self.assertIn("UNEVALUATED", (root / "b" / "report.html").read_text())


if __name__ == "__main__":
    unittest.main()
