#!/usr/bin/env python3
"""METHOD-077: the frozen analysis of tools/benchmark/cpd_landmark_variants.py on fabricated
records with known decisions (gates, timing noise, beats / worse / no decision, ranking)."""
from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("cpd_landmark_variants", REPOSITORY / "tools/benchmark/cpd_landmark_variants.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


def result(rms=1e-5, nystrom=20, seconds=100.0, truth=0.005, kernels=1000):
    return {"rms_to_exact": rms, "max_to_exact": rms * 3, "nystrom_iterations": nystrom, "seconds": seconds,
            "rms_to_truth": truth, "iterations": 150, "kernel_evaluations": kernels, "sigma2": 1e-5}


def records(edit=lambda pair, arm, run, r: r):
    """Every cell completed with passing gates: baseline 100 s, others as edited."""
    out = []
    for fixture in TOOL.FIXTURES:
        for seed in TOOL.SEEDS:
            pair = f"{fixture}/{seed}"
            cells = [("exact", None), ("floor_control", None), ("negative_control", None)]
            cells += [(arm, run) for arm in TOOL.VARIANTS + [TOOL.PERTURBATION] for run in (1, 2)]
            for arm, run in cells:
                r = result()
                if arm == "exact":
                    r["nystrom_iterations"] = 0
                if arm == "floor_control":
                    r.update(nystrom_iterations=0, rms_to_exact=1e-6)
                if arm == "negative_control":
                    r.update(rms_to_exact=0.05)
                if run == 2:
                    r["seconds"] = r["seconds"] * 1.01  # 1 % timing spread
                r = edit(pair, arm, run, r)
                cell = f"{pair}/{arm}" + (f"@{run}" if run else "")
                out.append({"cell": cell, "outcome": "completed", "result": r, "commit": "c", "runner_sha256": "r",
                            "points": TOOL.POINTS, "pinned": True})
    return out


def analyze(rows, expect=0):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "results.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows))
        code = TOOL.analyze(type("Args", (), {"out": str(path), "points": TOOL.POINTS, "expect_commit": "c",
                                              "expect_runner_sha256": "r"})())
        assert code == expect, code
        return json.loads((path / "analysis.json").read_text()) if code == 0 else None


class Analysis(unittest.TestCase):
    def test_decisions_follow_the_rule(self):
        def edit(pair, arm, run, r):
            if arm == "points/fixed/random":
                r["seconds"] *= 0.7            # 30 % faster, equivalent: beats
            if arm == "box/mixed/random":
                r["rms_to_exact"] = 5e-3       # not equivalent everywhere: worse
            if arm == "box/fixed/random":
                r["seconds"] *= 0.95           # only 5 % faster: no decision
            return r
        report = analyze(records(edit))
        self.assertTrue(report["ranking"])
        decisions = {v: d["decision"] for v, d in report["variants"].items()}
        self.assertEqual(decisions["points/fixed/random"], "beats baseline")
        self.assertEqual(decisions["box/mixed/random"], "worse than baseline")
        self.assertEqual(decisions["box/fixed/random"], "no decision")

    def test_timing_noise_blocks_a_small_win(self):
        # 30 % faster, but the arms' two passes differ by 20 %: 0.3 is not above 2 x 0.2.
        def edit(pair, arm, run, r):
            if arm == "points/fixed/random":
                r["seconds"] *= 0.7
            if arm == "box/fixed/halton" and run == 2:
                r["seconds"] *= 1.2
            return r
        report = analyze(records(edit))
        self.assertEqual(report["variants"]["points/fixed/random"]["decision"], "no decision")

    def test_failed_gates_leave_pairs_undecided(self):
        def floor(pair, arm, run, r):
            if pair == "full/1" and arm == "floor_control":
                r["rms_to_exact"] = 1e-3       # above the floor limit
            if pair == "full/2" and arm == "box/mixed/halton" and run == 2:
                r["kernel_evaluations"] += 1   # passes differ: not deterministic
            if pair == "full/3" and arm == "negative_control":
                r["nystrom_iterations"] = 0    # never approximated
            if arm == "points/fixed/random":
                r["seconds"] *= 0.7
            return r
        report = analyze(records(floor))
        decided = {p: v["decided"] for p, v in report["pairs"].items()}
        self.assertEqual(decided, {"full/1": False, "full/2": False, "full/3": False,
                                   "partial/1": True, "partial/2": True, "partial/3": True})
        self.assertFalse(report["ranking"])  # more than 2 of 6 pairs undecided
        self.assertEqual(report["variants"]["points/fixed/random"]["decision"], "no ranking")

    def test_a_failed_baseline_or_exact_cell_undecides_its_pair(self):
        rows = records(lambda pair, arm, run, r: r)
        rows = [dict(r, outcome="failed") if r["cell"] == "partial/2/exact" else r for r in rows]
        rows = [dict(r, outcome="failed") if r["cell"] == "partial/3/points/mixed/random@2" else r for r in rows]
        report = analyze(rows)
        self.assertFalse(report["pairs"]["partial/2"]["decided"])
        self.assertFalse(report["pairs"]["partial/3"]["decided"])
        self.assertTrue(report["ranking"])  # 4 of 6 decided

    def test_one_sided_outcomes_and_idle_perturbation_undecide_their_pairs(self):
        rows = records(lambda pair, arm, run, r: dict(r, nystrom_iterations=0) if pair == "full/2" and arm == TOOL.PERTURBATION else r)
        rows = [dict(r, outcome="failed") if r["cell"] == "full/1/box/fixed/halton@2" else r for r in rows]
        report = analyze(rows)
        self.assertFalse(report["pairs"]["full/1"]["decided"])  # one pass failed, the other completed
        self.assertFalse(report["pairs"]["full/2"]["decided"])  # perturbation control never approximated
        self.assertTrue(report["pairs"]["full/3"]["decided"])

    def test_predictions_without_evidence_are_not_evaluable(self):
        rows = records(lambda pair, arm, run, r: dict(r, rms_to_exact=5e-3) if arm == TOOL.BASELINE else r)
        report = analyze(rows)  # the baseline fails its gate everywhere: no ranking
        self.assertFalse(report["ranking"])
        for name in ("P1", "P2", "P3", "P4", "P5"):
            self.assertIsNone(report["predictions"][name], name)
        self.assertFalse(report["predictions"]["P6"])

    def test_mixed_provenance_is_refused(self):
        rows = records(lambda pair, arm, run, r: r)
        rows[5] = dict(rows[5], commit="other")
        self.assertIsNone(analyze(rows, expect=2))
        rows = [dict(r, pinned=False) for r in records(lambda pair, arm, run, r: r)]
        self.assertIsNone(analyze(rows, expect=2))
        # Uniformly another (or no) commit or runner, and a missing planned cell, are refused too.
        for field, value in (("commit", "other"), ("commit", None), ("runner_sha256", "other")):
            self.assertIsNone(analyze([dict(r, **{field: value}) for r in records(lambda pair, arm, run, r: r)], expect=2))
        self.assertIsNone(analyze(records(lambda pair, arm, run, r: r)[1:], expect=2))

    def test_a_failed_variant_counts_as_not_equivalent(self):
        # Failed in both passes: the same outcome (deterministic), counted as not equivalent.
        rows = [dict(r, outcome="failed") if r["cell"].split("/", 2)[2].startswith("box/resampled/random") else r
                for r in records(lambda pair, arm, run, r: r)]
        report = analyze(rows)
        self.assertEqual(report["variants"]["box/resampled/random"]["decision"], "worse than baseline")
        self.assertIsNone(report["predictions"]["P5"])  # a variant without measurements: not evaluable


if __name__ == "__main__":
    unittest.main(verbosity=1)
