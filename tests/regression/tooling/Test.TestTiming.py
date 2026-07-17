#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "tools" / "ci" / "collect_test_timing.py"
REGISTERED = {
    "AlphaTests": ("core", "unit"),
    "BetaTests": ("contract", "runtime"),
    "SlowTests": ("geometry", "slow"),
}


def _properties(labels: tuple[str, ...], disabled: bool = False) -> list[object]:
    records: list[object] = [{"name": "LABELS", "value": list(labels)}]
    if disabled:
        records.append({"name": "DISABLED", "value": True})
    return records


def _test_record(
    build_dir: Path,
    name: str,
    producer: str,
    labels: tuple[str, ...],
    *,
    disabled: bool = False,
) -> dict[str, object]:
    return {
        "command": [str(build_dir / "bin" / producer), f"--gtest_filter={name}"],
        "name": name,
        "properties": _properties(labels, disabled),
    }


def _write_build(build_dir: Path, *, aggregate: tuple[str, ...] | None = None) -> None:
    (build_dir / "bin").mkdir(parents=True)
    inventory = build_dir / "test-inventories"
    inventory.mkdir()
    registry = ["target\tlabels"]
    registry.extend(
        f"{target}\t{','.join(labels)}"
        for target, labels in sorted(REGISTERED.items())
    )
    (inventory / "RegisteredTestTargets.tsv").write_text(
        "\n".join(registry) + "\n", encoding="utf-8"
    )
    (inventory / "IntrinsicPrFastTests.txt").write_text(
        "\n".join(aggregate or ("AlphaTests", "BetaTests")) + "\n",
        encoding="utf-8",
    )
    (inventory / "IntrinsicCpuSlowTests.txt").write_text(
        "SlowTests\n",
        encoding="utf-8",
    )
    tests = [
        _test_record(
            build_dir, "Alpha.Fast", "AlphaTests", REGISTERED["AlphaTests"]
        ),
        _test_record(
            build_dir,
            "Alpha.Disabled",
            "AlphaTests",
            REGISTERED["AlphaTests"],
            disabled=True,
        ),
        _test_record(
            build_dir, "Beta.Contract", "BetaTests", REGISTERED["BetaTests"]
        ),
        _test_record(
            build_dir, "Slow.Stress", "SlowTests", REGISTERED["SlowTests"]
        ),
    ]
    (build_dir / "ctest.json").write_text(
        json.dumps({"kind": "ctestInfo", "tests": tests}),
        encoding="utf-8",
    )


def _write_fake_ctest(path: Path) -> None:
    path.write_text(
        f"""#!{sys.executable}
import html
import json
import os
import re
import sys
from pathlib import Path

arguments = sys.argv[1:]
build = Path(arguments[arguments.index("--test-dir") + 1])
if "--show-only=json-v1" in arguments:
    print((build / "ctest.json").read_text(encoding="utf-8"))
    raise SystemExit(0)

commands = build / "commands.jsonl"
with commands.open("a", encoding="utf-8") as handle:
    handle.write(json.dumps(arguments) + "\\n")
counter = build / "sample-count.txt"
sample = int(counter.read_text(encoding="ascii")) + 1 if counter.exists() else 1
counter.write_text(str(sample), encoding="ascii")
cost = build / "Testing" / "Temporary" / "CTestCostData.txt"
cost.parent.mkdir(parents=True, exist_ok=True)
cost.write_text(f"mutated-{{sample}}\\n", encoding="utf-8")
if (build / "drift").exists() and sample == 1:
    document = json.loads((build / "ctest.json").read_text(encoding="utf-8"))
    document["tests"] = [
        test for test in document["tests"] if test["name"] != "Beta.Contract"
    ]
    (build / "ctest.json").write_text(json.dumps(document), encoding="utf-8")

junit = Path(arguments[arguments.index("--output-junit") + 1])
failed = (build / "fail").exists() and sample == 2
exclude = arguments[arguments.index("-LE") + 1]
if "-L" in arguments and re.search(
    arguments[arguments.index("-L") + 1], "slow"
):
    cases = [] if re.search(exclude, "slow") else [
        ("Slow.Stress", "geometry;slow", "run", 3000 + sample, ""),
    ]
else:
    cases = [
        ("Alpha.Fast", "core;unit", "run", 1000 + sample, "failure" if failed else ""),
        ("Alpha.Disabled", "core;unit", "disabled", 0, ""),
        ("Beta.Contract", "contract;runtime", "run", 2000 + sample, "skipped" if sample == 3 else ""),
    ]
lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<testsuite name="timing">']
for name, labels, status, micros, outcome in cases:
    lines.append(
        f'<testcase name="{{html.escape(name)}}" classname="{{html.escape(name)}}" '
        f'time="{{micros / 1_000_000:.6f}}" status="{{status}}">'
    )
    lines.append(
        f'<properties><property name="cmake_labels" '
        f'value="{{html.escape(labels)}}"/></properties>'
    )
    if outcome:
        lines.append(f'<{{outcome}} message="{{outcome}}"/>')
    lines.append("</testcase>")
lines.append("</testsuite>")
junit.parent.mkdir(parents=True, exist_ok=True)
junit.write_text("\\n".join(lines) + "\\n", encoding="utf-8")
print(f"sample {{sample}}")
raise SystemExit(8 if failed else 0)
""",
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class TestTimingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.fake_bin = self.root / "fake-bin"
        self.fake_bin.mkdir()
        _write_fake_ctest(self.fake_bin / "ctest")
        self.environment = os.environ.copy()
        self.environment["PATH"] = (
            f"{self.fake_bin}{os.pathsep}{self.environment.get('PATH', '')}"
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _run(
        self,
        build: Path,
        output: Path,
        *,
        cohort: str = "pr-fast",
        samples: int = 5,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--build-dir",
                str(build),
            "--cohort",
            cohort,
                "--samples",
                str(samples),
                "--parallel",
                "4",
                "--output",
                str(output),
            ],
            cwd=REPO_ROOT,
            env=self.environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_collects_reconciled_per_case_timing_and_restores_cost_data(self) -> None:
        build = self.root / "build"
        output = self.root / "timing"
        _write_build(build)
        cost = build / "Testing" / "Temporary" / "CTestCostData.txt"
        cost.parent.mkdir(parents=True)
        cost.write_text("baseline\n", encoding="utf-8")

        result = self._run(build, output)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(cost.read_text(encoding="utf-8"), "baseline\n")

        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["schema"], "intrinsic.test-timing/v1")
        self.assertRegex(report["selection_digest"], r"^[0-9a-f]{64}$")
        self.assertEqual(
            report["identity"],
            {
                "aggregate": "IntrinsicPrFastTests",
                "cohort": "pr-fast",
                "parallel_jobs": 4,
                "selector": {
                    "exclude_any": [
                        "flaky-quarantine",
                        "gpu",
                        "slow",
                        "vulkan",
                    ],
                    "include_any": ["contract", "unit"],
                },
                "timeout_seconds": 60,
            },
        )
        self.assertEqual(report["summary"]["sample_count"], 5)
        self.assertEqual(report["summary"]["selected_test_count"], 3)
        self.assertEqual(report["summary"]["disabled_result_count"], 5)
        self.assertEqual(report["summary"]["skipped_result_count"], 1)
        self.assertTrue(report["summary"]["valid"])
        cases = {record["name"]: record for record in report["cases"]}
        self.assertEqual(cases["Alpha.Fast"]["executable"], "AlphaTests")
        self.assertEqual(
            cases["Alpha.Fast"]["duration_us"],
            {
                "median": 1003,
                "p95": 1005,
                "samples": [1001, 1002, 1003, 1004, 1005],
            },
        )
        self.assertEqual(
            cases["Beta.Contract"]["statuses"],
            ["passed", "passed", "skipped", "passed", "passed"],
        )
        for index in range(1, 6):
            self.assertTrue(
                (output / "samples" / f"sample-{index:02d}.junit.xml").is_file()
            )
            self.assertTrue(
                (output / "samples" / f"sample-{index:02d}.log").is_file()
            )

        commands = [
            json.loads(line)
            for line in (build / "commands.jsonl").read_text(encoding="utf-8").splitlines()
        ]
        self.assertEqual(len(commands), 5)
        for command in commands:
            self.assertEqual(
                command[command.index("--parallel") + 1],
                "4",
            )
            self.assertEqual(command[command.index("-L") + 1], "^(contract|unit)$")
            self.assertEqual(
                command[command.index("-LE") + 1],
                "^(flaky-quarantine|gpu|slow|vulkan)$",
            )

    def test_cpu_slow_selector_anchors_slo_exclusion(self) -> None:
        build = self.root / "build"
        output = self.root / "timing"
        _write_build(build)

        result = self._run(build, output, cohort="cpu-slow", samples=1)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["summary"]["selected_test_count"], 1)
        self.assertEqual(report["cases"][0]["name"], "Slow.Stress")
        command = json.loads(
            (build / "commands.jsonl").read_text(encoding="utf-8").strip()
        )
        self.assertEqual(command[command.index("-L") + 1], "^(slow)$")
        self.assertEqual(
            command[command.index("-LE") + 1],
            "^(benchmark|flaky-quarantine|gpu|slo|vulkan)$",
        )

    def test_failed_sample_writes_actionable_report_and_returns_blocked(self) -> None:
        build = self.root / "build"
        output = self.root / "timing"
        _write_build(build)
        (build / "fail").touch()

        result = self._run(build, output)
        self.assertEqual(result.returncode, 3)
        self.assertIn("measured CTest samples failed", result.stderr)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertFalse(report["summary"]["valid"])
        self.assertEqual(report["summary"]["ctest_nonzero_sample_count"], 1)
        self.assertEqual(report["summary"]["failed_result_count"], 1)
        alpha = next(
            record for record in report["cases"] if record["name"] == "Alpha.Fast"
        )
        self.assertEqual(alpha["statuses"][1], "failed")

    def test_rejects_aggregate_drift_and_nonempty_output(self) -> None:
        build = self.root / "build"
        _write_build(build, aggregate=("AlphaTests",))
        output = self.root / "timing"
        result = self._run(build, output, samples=1)
        self.assertEqual(result.returncode, 3)
        self.assertIn("missing=['BetaTests']", result.stderr)

        clean_build = self.root / "clean-build"
        _write_build(clean_build)
        nonempty = self.root / "nonempty"
        nonempty.mkdir()
        (nonempty / "stale.json").write_text("{}\n", encoding="utf-8")
        result = self._run(clean_build, nonempty, samples=1)
        self.assertEqual(result.returncode, 3)
        self.assertIn("must be absent or empty", result.stderr)

    def test_rejects_inventory_drift_during_sampling(self) -> None:
        build = self.root / "build"
        output = self.root / "timing"
        _write_build(build)
        (build / "drift").touch()

        result = self._run(build, output, samples=1)
        self.assertEqual(result.returncode, 3)
        self.assertIn("producers have no selected CTest cases", result.stderr)
        self.assertTrue((output / "samples" / "sample-01.junit.xml").is_file())
        self.assertFalse((output / "report.json").exists())


if __name__ == "__main__":
    unittest.main()
