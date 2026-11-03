#!/usr/bin/env python3
"""Self-test of the R4 evaluation (r4_driver.summarize, report; r4_report): synthetic records with known answers.
Usage: python3 -B plugins/fixture/r4_selftest.py
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


class Closes(unittest.TestCase):
    def test_only_a_new_clean_close_of_the_generation_counts(self) -> None:
        clean = {"generation": 7, "closed": True, "unmapped": True, "live": {"operation": 0}}
        self.assertTrue(r4_driver.new_clean_close(2, [{}, {}, clean], 7))
        self.assertFalse(r4_driver.new_clean_close(3, [{}, {}, clean], 7))   # reused: no new record
        self.assertFalse(r4_driver.new_clean_close(2, [{}, {}, clean], 8))   # another generation
        self.assertFalse(r4_driver.new_clean_close(2, [{}, {}, dict(clean, unmapped=False)], 7))
        self.assertFalse(r4_driver.new_clean_close(2, [{}, {}, dict(clean, live={"operation": 1})], 7))

    def test_step_durations_must_be_finite(self) -> None:
        self.assertTrue(r4_driver.finite(0.5))
        for value in (None, float("nan"), float("inf"), "1.0"):
            self.assertFalse(r4_driver.finite(value), value)


class BuildGroups(unittest.TestCase):
    def test_a_terminated_driver_kills_its_running_build_group(self) -> None:
        import os
        import signal
        import subprocess
        import time
        with tempfile.TemporaryDirectory() as directory:
            pid_file = Path(directory) / "pid"
            child = subprocess.Popen([sys.executable, "-B", "-c", (
                "import signal, sys; sys.path.insert(0, %r); import r4_driver; "
                "signal.signal(signal.SIGTERM, r4_driver.stop_active_group); "
                "r4_driver.run_group(['bash', '-c', 'echo $$ > %s; exec sleep 30'], 60)"
            ) % (str(Path(__file__).resolve().parent), pid_file)])
            for _ in range(100):
                if pid_file.exists() and pid_file.read_text().strip():
                    break
                time.sleep(0.05)
            group_leader = int(pid_file.read_text())
            child.send_signal(signal.SIGTERM)
            self.assertEqual(child.wait(timeout=10), 128 + signal.SIGTERM)
            with self.assertRaises(ProcessLookupError):
                os.kill(group_leader, 0)  # the build group's process is gone

    def test_the_inner_timeout_kills_the_group(self) -> None:
        timed_out, code, _ = r4_driver.run_group(["bash", "-c", "sleep 30"], 0.3)
        self.assertTrue(timed_out)
        self.assertIsNone(code)


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
            expect = ["--expect", "1,2,0"]
            ok = subprocess.run([sys.executable, "-B", str(script), *expect, str(root / "a"), str(root / "run1"),
                                 str(root / "run2")], capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            summary = json.loads((root / "a" / "summary.json").read_text())
            self.assertTrue(summary["valid"])
            self.assertEqual(summary["summary"]["total"], {"median": 5.5, "min": 4.0, "max": 7.0})
            (root / "run2" / "summary.json").write_text(json.dumps({"failed": ["validity: something"]}))
            bad = subprocess.run([sys.executable, "-B", str(script), *expect, str(root / "b"), str(root / "run1"),
                                  str(root / "run2")], capture_output=True, text=True)
            self.assertEqual(bad.returncode, 1)
            combined = json.loads((root / "b" / "summary.json").read_text())
            self.assertFalse(combined["valid"])
            self.assertEqual(combined["summary"]["verdict"], "UNEVALUATED")
            self.assertIn("UNEVALUATED", (root / "b" / "report.html").read_text())

    def test_a_run_with_other_counts_than_frozen_invalidates_the_pool(self) -> None:
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run = root / "run1"
            run.mkdir()
            lines = [record(0, 40.0, "warmup")] + [record(i, 5.0) for i in (1, 2)]
            (run / "samples.jsonl").write_text("".join(json.dumps(r) + "\n" for r in lines))
            (run / "summary.json").write_text(json.dumps({"failed": []}))
            script = Path(__file__).resolve().parent / "r4_report.py"
            short = subprocess.run([sys.executable, "-B", str(script), str(root / "a"), str(run)],
                                   capture_output=True, text=True)
            self.assertEqual(short.returncode, 1)  # the frozen 1, 5, 3 expects five measured samples and the probes
            self.assertEqual(json.loads((root / "a" / "summary.json").read_text())["summary"]["verdict"], "UNEVALUATED")

    def test_plugin_steps_are_summarized_only_when_every_sample_has_them(self) -> None:
        records = [dict(record(i, 5.0), compile_s=3.0 + i, link_s=0.5) for i in range(3)]
        self.assertEqual(r4_driver.summarize(records)["compile_s"], {"median": 4.0, "min": 3.0, "max": 5.0})
        records[1]["compile_s"] = None
        self.assertNotIn("compile_s", r4_driver.summarize(records))

    def test_plugin_edge_classification(self) -> None:
        for edge in ("Building CXX object plugins/fixture/CMakeFiles/IntrinsicFixturePlugin.dir/FixturePlugin.cpp.o",
                     "Linking CXX shared module plugins/libIntrinsicFixturePlugin.so",
                     "Re-checking globbed directories...",
                     "Scanning /home/alex/Documents/IntrinsicEngine/plugins/fixture/FixturePlugin.cpp for CXX dependencies",
                     "Generating CXX dyndep file plugins/fixture/CMakeFiles/IntrinsicFixturePlugin.dir/CXX.dd"):
            self.assertTrue(r4_driver.plugin_edge(edge), edge)
        for edge in ("Linking CXX executable bin/ExtrinsicSandbox", "Building CXX object src/runtime/CMakeFiles/x.o",
                     "Plugin build id",
                     "Linking CXX shared library lib/libExtrinsicRuntime.so"):
            self.assertFalse(r4_driver.plugin_edge(edge), edge)


if __name__ == "__main__":
    unittest.main()
