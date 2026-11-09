#!/usr/bin/env python3
"""Known-answer tests for campaign custody and completed-work reporting; no GPU measurements."""
from __future__ import annotations

import copy
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "benchmark"))
import method_execution_campaign as campaign
import method_execution_report as report


def observation(queue="graphics", preview=0, buffers=3):
    return {"config": {"mode": "kmeans", "points": 100000, "seed": 11, "queue": queue,
                       "preview_every": preview, "preview_buffers": buffers},
            "cloud_hash": 123, "runs": [{"repeat": i, "warmup": i == 0, "labels_hash": 456,
                "iterations": 4, "converged": False, "ready_computed_ms": 20.,
                "quality_passed": True, "quality_error_linf": 0.} for i in range(4)],
            "frames": [{"time_ms": 1., "total_us": 1000., "readback_ring_high_water_bytes": 64,
                        "resident_bytes": 64, "index_bytes": 0, "scratch_active_bytes": 0,
                        "scratch_pending_bytes": 0, "scratch_idle_bytes": 0}]}


def queries():
    fixture = observation()
    fixture["config"]["mode"] = "queries"
    fixture["config"]["strategy"] = "brute_force"
    for run in fixture["runs"]:
        run.update(runtime_ms=20., completed_queries=256,
                   page_samples=[{"first": i, "completed_queries": i + 16} for i in range(0, 256, 16)])
    return fixture


class CampaignTests(unittest.TestCase):
    def test_fixed_budget_and_balanced_pairs(self):
        cells = campaign.cells()
        self.assertEqual(len(cells), 94)
        self.assertEqual(len({c["id"] for c in cells}), 94)
        self.assertEqual(sum(c["timeout_seconds"] for c in cells) + 60, 1740)
        for seed in campaign.SEEDS:
            for count in (100000, 1000000):
                arms = [c for c in cells if c["mode"] == "kmeans" and c["seed"] == seed and c["points"] == count]
                self.assertEqual({(c["queue"], c["preview_every"], c["preview_buffers"]) for c in arms},
                                 {(q, p, b) for q in ("graphics", "separate") for p, b in ((0, 3), (1, 2), (1, 3))})

    def test_only_completed_quality_checked_work_contributes(self):
        fixture = observation()
        result = report.summary({"cell": fixture}, "kmeans")
        self.assertEqual(result["throughput_items_per_sec"]["cell"], 20000000.)
        self.assertEqual(result["sample_count"], 1)
        self.assertIsNone(report.summary({}, "kmeans"))
        for key, value in (("iterations", 0), ("quality_passed", False), ("ready_computed_ms", 0)):
            poison = copy.deepcopy(fixture)
            poison["runs"][1][key] = value
            with self.assertRaises(ValueError):
                report.summary({"cell": poison}, "kmeans")
        fixture["runs"][1]["warmup"] = True
        with self.assertRaises(ValueError):
            report.summary({"cell": fixture}, "kmeans")

    def test_pairing_rejects_different_completed_work_and_output(self):
        left, right = observation(), observation("separate")
        self.assertEqual(report.paired_errors({"left": left, "right": right}), [])
        for key, value in (("labels_hash", 999), ("iterations", 3), ("converged", True)):
            poison = copy.deepcopy(right)
            poison["runs"][1][key] = value
            self.assertEqual(len(report.paired_errors({"left": left, "right": poison})), 1)

    def test_exact_repeat_and_page_coverage(self):
        for fixture in (observation(), queries()):
            self.assertEqual(len(report.validated_runs(fixture)), 3)
            for index in range(4):
                missing = copy.deepcopy(fixture)
                del missing["runs"][index]
                with self.assertRaises(ValueError):
                    report.summary({"cell": missing}, missing["config"]["mode"])
            duplicate = copy.deepcopy(fixture)
            duplicate["runs"][2]["repeat"] = 1
            with self.assertRaises(ValueError):
                report.validated_runs(duplicate)
        control = observation()
        control["config"]["mode"] = "controls"
        control["runs"] = [dict(control["runs"][0], warmup=False)]
        self.assertEqual(len(report.validated_runs(control)), 1)
        control["runs"][0]["repeat"] = 1
        with self.assertRaises(ValueError):
            report.validated_runs(control)
        self.assertEqual(report.summary({"queries": queries()}, "queries")["throughput_items_per_sec"]["queries"], 12800.)
        short = queries()
        short["runs"][1]["completed_queries"] = 240
        with self.assertRaises(ValueError):
            report.summary({"cell": short}, "queries")
        for mutate in (lambda pages: pages.pop(), lambda pages: pages.__setitem__(1, pages[0]),
                       lambda pages: pages[0].update(completed_queries=0)):
            short = queries()
            mutate(short["runs"][1]["page_samples"])
            with self.assertRaises(ValueError):
                report.validated_runs(short)

    def test_queue_and_preview_axes_are_not_conflated(self):
        payloads = {}
        for queue, queue_cost in (("graphics", 1.), ("separate", .5)):
            for preview, buffers, preview_cost in ((0, 3, 1.), (1, 2, 2.), (1, 3, 3.)):
                payload = observation(queue, preview, buffers)
                for run in payload["runs"]:
                    run["ready_computed_ms"] = 20 * queue_cost * preview_cost
                payloads[str((queue, preview, buffers))] = payload
        paired = report.paired_distribution(payloads)
        self.assertEqual(len(paired), 7)
        queue_ratios = [value["median_ratio"] for key, value in paired.items() if "queue:" in key]
        preview_ratios = [value["median_ratio"] for key, value in paired.items() if "preview:" in key]
        self.assertEqual(queue_ratios, [.5, .5, .5])
        self.assertEqual(sorted(preview_ratios), [2., 2., 3., 3.])

    def test_execution_identity_is_unique_but_rerender_stable(self):
        cell = next(c for c in campaign.cells() if c["mode"] == "kmeans")
        manifest = "benchmarks/rendering/manifests/runtime_method_execution_kmeans.yaml"
        packet = {"cells": [cell], "source": {"state": "clean_commit", "revision": "a" * 40,
                  "files": {manifest: campaign.file_digest(ROOT / manifest)}}, "environment": {}}
        with tempfile.TemporaryDirectory() as name:
            identities = []
            for execution in ("first", "second"):
                root = Path(name) / execution
                payload = observation()
                payload.update(config=cell, technical_passed=True, complete=True)
                campaign.write(root / "packet.json", packet)
                campaign.write(root / "configs" / (cell["id"] + ".json"), cell)
                raw = root / "raw" / (cell["id"] + ".json")
                campaign.write(raw, payload)
                ledger = campaign.new_outcomes(packet)
                ledger["cells"] = [{"id": cell["id"], "status": "passed", "output": str(raw.relative_to(root)),
                                    "output_sha256": campaign.file_digest(raw),
                                    "config_sha256": campaign.file_digest(root / "configs" / (cell["id"] + ".json"))}]
                campaign.write(root / "outcomes.json", ledger)
                report.render(root)
                result = campaign.read(root / "results" / "kmeans.json")
                identity = result["run_id"], result["attempt_id"]
                report.render(root)
                rerendered = campaign.read(root / "results" / "kmeans.json")
                self.assertEqual(identity, (rerendered["run_id"], rerendered["attempt_id"]))
                identities.append(identity)
            self.assertNotEqual(identities[0], identities[1])
        with self.assertRaises(ValueError):
            report.canonical_identity({"execution_id": ""})

    def test_failed_output_is_bound_rendered_and_not_summarized(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            cell = next(c for c in campaign.cells() if c["mode"] == "kmeans")
            reference = root / "refs" / f"reference-{cell['points']}-{cell['seed']}.json"
            campaign.write(reference, {})
            packet = {"cells": [cell], "source": {"executable": "unused", "files": {}}, "environment": {}}
            payload = observation()
            payload.update(technical_passed=False, complete=True, failure="poisoned quality rejected")

            def failed_process(command, env, timeout, stdout, stderr):
                payload["config"] = campaign.read(env["INTRINSIC_METHOD_EXECUTION_CONFIG"])
                campaign.write(env["INTRINSIC_METHOD_EXECUTION_OUT"], payload)
                return 1

            with mock.patch.object(campaign, "validate_packet"), mock.patch.object(campaign, "bounded", failed_process):
                outcome = campaign.execute_cell(packet, cell, root, time.monotonic() + 5)
            self.assertEqual(outcome["status"], "error")
            self.assertEqual(outcome["output_sha256"], campaign.file_digest(root / outcome["output"]))
            ledger = campaign.new_outcomes(packet)
            ledger["cells"] = [outcome]
            campaign.write(root / "packet.json", packet)
            campaign.write(root / "outcomes.json", ledger)
            passed, failed = report.load_observations(root, packet, ledger)
            self.assertEqual(passed, {})
            self.assertEqual(len(failed), 1)
            self.assertEqual(report.render(root), 1)
            rendered = (root / "report.html").read_text()
            self.assertIn("FAILED — EXCLUDED FROM PASS SUMMARIES", rendered)
            self.assertIn("poisoned quality rejected", rendered)
            self.assertIn("total_us over process time", rendered)
            self.assertFalse((root / "results").exists())
            with (root / outcome["output"]).open("a") as stream:
                stream.write(" ")
            with self.assertRaises(ValueError):
                report.load_observations(root, packet, ledger)

    def test_reviews_require_exact_packet_independent_sessions_and_transcripts(self):
        packet = {"technical_only": False, "source": {"state": "clean_commit"}}
        with tempfile.TemporaryDirectory() as name:
            transcript = Path(name) / "review.txt"
            transcript.write_text("Known-answer review fixture")
            answers = {"reviews": [{"provider": provider, "session": provider + "-session", "author": False,
                       "verdict": "PASS", "packet_sha256": campaign.digest(packet), "transcript": str(transcript),
                       "transcript_sha256": campaign.file_digest(transcript)} for provider in ("claude", "codex")]}
            campaign.check_reviews(packet, answers)
            for field, value in (("author", True), ("packet_sha256", "wrong"), ("verdict", "MISSING"), ("session", "claude-session")):
                poison = copy.deepcopy(answers)
                poison["reviews"][1][field] = value
                with self.assertRaises(ValueError):
                    campaign.check_reviews(packet, poison)
            transcript.write_text("Changed")
            with self.assertRaises(ValueError):
                campaign.check_reviews(packet, answers)

    @unittest.skipUnless(os.name == "posix", "Campaign uses POSIX process groups")
    def test_timeout_kills_descendants(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            marker = root / "child-survived"
            ready = root / "child-started"
            child = "import time; from pathlib import Path; Path(" + repr(str(ready)) + ").touch(); time.sleep(1.5); Path(" + repr(str(marker)) + ").touch()"
            parent = "import subprocess,sys,time; subprocess.Popen([sys.executable,'-c'," + repr(child) + "]); time.sleep(30)"
            with self.assertRaises(TimeoutError):
                campaign.bounded([sys.executable, "-c", parent], os.environ, 1., root / "out", root / "err")
            self.assertTrue(ready.exists(), "The descendant must start before this test can prove group cleanup")
            time.sleep(.8)
            self.assertFalse(marker.exists(), "Timed-out descendant survived and performed delayed work")


if __name__ == "__main__":
    unittest.main()
