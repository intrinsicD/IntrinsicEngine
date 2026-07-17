#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "tools" / "ci" / "cpu_test_selection.py"
EXCLUDED_LABELS = ["flaky-quarantine", "gpu", "slow", "vulkan"]
SANITIZER_CONFIG = {
    "none": ("none", "OFF", "OFF", "OFF"),
    "asan": ("address", "ON", "ON", "OFF"),
    "ubsan": ("undefined", "ON", "OFF", "ON"),
    "asan-ubsan": ("address,undefined", "ON", "ON", "ON"),
}
REGISTERED = {
    "AlphaTests": ("core", "unit"),
    "BetaTests": ("contract", "runtime"),
    "GpuTests": ("gpu", "graphics"),
    "SlowTests": ("geometry", "slow"),
}


def _canonical_digest(value: object) -> str:
    encoded = json.dumps(
        value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


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
    wrapper: bool = False,
    disabled: bool = False,
) -> dict[str, object]:
    binary = build_dir / "bin" / producer
    command = (
        ["/usr/bin/cmake", f"-DPROBE_EXE={binary}", "-P", "fixture.cmake"]
        if wrapper
        else [str(binary), f"--gtest_filter={name}"]
    )
    return {
        "command": command,
        "name": name,
        "properties": _properties(labels, disabled),
    }


def _default_tests(build_dir: Path) -> list[dict[str, object]]:
    return [
        _test_record(build_dir, "Alpha.First", "AlphaTests", REGISTERED["AlphaTests"]),
        _test_record(
            build_dir,
            "Alpha.Second",
            "AlphaTests",
            REGISTERED["AlphaTests"],
            disabled=True,
        ),
        _test_record(
            build_dir,
            "Beta.Contract",
            "BetaTests",
            REGISTERED["BetaTests"],
            wrapper=True,
        ),
        _test_record(build_dir, "Gpu.Draw", "GpuTests", REGISTERED["GpuTests"]),
        _test_record(build_dir, "Slow.Long", "SlowTests", REGISTERED["SlowTests"]),
    ]


def _write_build(
    build_dir: Path,
    sanitizer: str,
    *,
    aggregate: tuple[str, ...] = ("AlphaTests", "BetaTests"),
    registered: dict[str, tuple[str, ...]] | None = None,
    tests: list[dict[str, object]] | None = None,
) -> None:
    registered = REGISTERED if registered is None else registered
    build_dir.mkdir(parents=True)
    (build_dir / "bin").mkdir()
    inventory_dir = build_dir / "test-inventories"
    inventory_dir.mkdir()

    mode, enabled, address, undefined = SANITIZER_CONFIG[sanitizer]
    (build_dir / "CMakeCache.txt").write_text(
        "\n".join(
            (
                f"INTRINSIC_ENABLE_SANITIZERS:BOOL={enabled}",
                f"INTRINSIC_SANITIZER_MODE:STRING={mode}",
                f"INTRINSIC_SANITIZER_IDENTITY:INTERNAL={sanitizer}",
                f"INTRINSIC_SANITIZER_HAS_ADDRESS:INTERNAL={address}",
                f"INTRINSIC_SANITIZER_HAS_UNDEFINED:INTERNAL={undefined}",
            )
        )
        + "\n",
        encoding="utf-8",
    )
    registry_lines = ["target\tlabels"]
    registry_lines.extend(
        f"{target}\t{','.join(labels)}"
        for target, labels in sorted(registered.items())
    )
    (inventory_dir / "RegisteredTestTargets.tsv").write_text(
        "\n".join(registry_lines) + "\n", encoding="utf-8"
    )
    (inventory_dir / "IntrinsicCpuTests.txt").write_text(
        "\n".join(aggregate) + "\n", encoding="utf-8"
    )
    document = {"kind": "ctestInfo", "tests": tests or _default_tests(build_dir)}
    (build_dir / "ctest.json").write_text(
        json.dumps(document), encoding="utf-8"
    )


class CpuTestSelectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.fake_bin = self.root / "fake-bin"
        self.fake_bin.mkdir()
        fake_ctest = self.fake_bin / "ctest"
        fake_ctest.write_text(
            "\n".join(
                (
                    f"#!{sys.executable}",
                    "import json",
                    "import sys",
                    "from pathlib import Path",
                    "arguments = sys.argv[1:]",
                    "build = Path(arguments[arguments.index('--test-dir') + 1])",
                    "print((build / 'ctest.json').read_text(encoding='utf-8'))",
                )
            )
            + "\n",
            encoding="utf-8",
        )
        fake_ctest.chmod(fake_ctest.stat().st_mode | stat.S_IXUSR)
        self.environment = os.environ.copy()
        self.environment["PATH"] = (
            f"{self.fake_bin}{os.pathsep}{self.environment.get('PATH', '')}"
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _run(
        self,
        *arguments: str,
        environment: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(TOOL), *arguments],
            cwd=REPO_ROOT,
            env=self.environment if environment is None else environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def _capture(
        self,
        build_dir: Path,
        preset: str,
        sanitizer: str,
        *,
        output_name: str | None = None,
        check: bool = True,
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        output = self.root / (output_name or f"{preset}.json")
        result = self._run(
            "capture",
            "--build-dir",
            str(build_dir),
            "--preset",
            preset,
            "--expected-sanitizer",
            sanitizer,
            "--output",
            str(output),
        )
        if check:
            self.assertEqual(result.returncode, 0, msg=result.stderr)
        return result, output

    def test_capture_records_exact_path_free_selection_and_github_outputs(
        self,
    ) -> None:
        build = self.root / "deep" / "absolute" / "build"
        _write_build(build, "none")
        github_output = self.root / "github-output.txt"
        environment = self.environment.copy()
        environment["GITHUB_OUTPUT"] = str(github_output)
        output = self.root / "capture.json"
        result = self._run(
            "capture",
            "--build-dir",
            str(build),
            "--preset",
            "ci",
            "--sanitizer",
            "none",
            "--output",
            str(output),
            environment=environment,
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)

        report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(report["schema"], "intrinsic.cpu-test-selection/v1")
        self.assertEqual(report["identity"]["preset"], "ci")
        self.assertEqual(report["identity"]["sanitizer"], "none")
        self.assertEqual(
            report["identity"]["selector"],
            {"kind": "exclude-labels", "labels": EXCLUDED_LABELS},
        )
        self.assertEqual(
            report["summary"],
            {
                "disabled_test_count": 1,
                "producer_count": 2,
                "selected_test_count": 3,
            },
        )
        normalized = report["selection"]["normalized"]
        self.assertEqual(
            [record["name"] for record in normalized["producers"]],
            ["AlphaTests", "BetaTests"],
        )
        self.assertEqual(
            [record["name"] for record in normalized["tests"]],
            ["Alpha.First", "Alpha.Second", "Beta.Contract"],
        )
        self.assertEqual(
            report["selection"]["digest"], _canonical_digest(normalized)
        )
        self.assertNotIn(str(build.resolve()), output.read_text(encoding="utf-8"))

        outputs = github_output.read_text(encoding="utf-8")
        self.assertIn("producer-count=2\n", outputs)
        self.assertIn("selected-test-count=3\n", outputs)
        self.assertIn(
            f"selection-digest={report['selection']['digest']}\n", outputs
        )

    def test_compare_accepts_path_independent_required_variant_parity(self) -> None:
        reports: list[Path] = []
        for preset, sanitizer in (
            ("ci", "none"),
            ("ci-asan", "asan"),
            ("ci-ubsan", "ubsan"),
        ):
            build = self.root / sanitizer / "different" / "build"
            _write_build(build, sanitizer)
            _result, report = self._capture(build, preset, sanitizer)
            reports.append(report)

        output = self.root / "parity.json"
        arguments = ["compare"]
        for report in reports:
            arguments.extend(("--report", str(report)))
        for sanitizer in ("none", "asan", "ubsan"):
            arguments.extend(("--require-sanitizer", sanitizer))
        arguments.extend(("--output", str(output)))
        result = self._run(*arguments)
        self.assertEqual(result.returncode, 0, msg=result.stderr)

        parity = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(
            parity["schema"], "intrinsic.cpu-test-selection-parity/v1"
        )
        self.assertEqual(parity["summary"]["selected_test_count"], 3)
        self.assertEqual(parity["summary"]["variant_count"], 3)
        self.assertEqual(
            parity["variants"],
            [
                {"preset": "ci", "sanitizer": "none"},
                {"preset": "ci-asan", "sanitizer": "asan"},
                {"preset": "ci-ubsan", "sanitizer": "ubsan"},
            ],
        )
        parity_text = output.read_text(encoding="utf-8")
        for report in reports:
            self.assertNotIn(str(report.parent.resolve()), parity_text)

    def test_capture_rejects_expected_sanitizer_mismatch(self) -> None:
        build = self.root / "build"
        _write_build(build, "asan")
        result, output = self._capture(
            build, "ci-asan", "ubsan", check=False
        )
        self.assertEqual(result.returncode, 3)
        self.assertIn("configured sanitizer identity is 'asan'", result.stderr)
        self.assertFalse(output.exists())

    def test_capture_rejects_aggregate_predicate_drift(self) -> None:
        build = self.root / "build"
        _write_build(build, "none", aggregate=("AlphaTests",))
        result, _output = self._capture(build, "ci", "none", check=False)
        self.assertEqual(result.returncode, 3)
        self.assertIn("missing=['BetaTests']", result.stderr)

    def test_capture_rejects_ctest_label_drift(self) -> None:
        build = self.root / "build"
        tests = _default_tests(build)
        tests[0]["properties"] = _properties(("core", "regression", "unit"))
        _write_build(build, "none", tests=tests)
        result, _output = self._capture(build, "ci", "none", check=False)
        self.assertEqual(result.returncode, 3)
        self.assertIn("labels differ from producer", result.stderr)

    def test_capture_rejects_selected_case_without_registered_producer(self) -> None:
        build = self.root / "build"
        tests = _default_tests(build)
        tests.append(
            {
                "command": [str(build / "bin" / "UnknownTests")],
                "name": "Unknown.Case",
                "properties": _properties(("core", "unit")),
            }
        )
        _write_build(build, "none", tests=tests)
        result, _output = self._capture(build, "ci", "none", check=False)
        self.assertEqual(result.returncode, 3)
        self.assertIn("does not map to a registered producer", result.stderr)

    def test_compare_rejects_exact_case_drift(self) -> None:
        first_build = self.root / "first"
        second_build = self.root / "second"
        _write_build(first_build, "none")
        changed_tests = _default_tests(second_build)
        changed_tests[0]["name"] = "Alpha.Renamed"
        changed_tests[0]["command"][-1] = "--gtest_filter=Alpha.Renamed"
        _write_build(second_build, "asan", tests=changed_tests)
        _result, first = self._capture(first_build, "ci", "none")
        _result, second = self._capture(second_build, "ci-asan", "asan")

        output = self.root / "parity.json"
        result = self._run(
            "compare",
            "--report",
            str(first),
            "--report",
            str(second),
            "--output",
            str(output),
        )
        self.assertEqual(result.returncode, 3)
        self.assertIn("missing_tests=['Alpha.First']", result.stderr)
        self.assertIn("extra_tests=['Alpha.Renamed']", result.stderr)
        self.assertFalse(output.exists())

    def test_compare_rejects_corrupt_digest_and_incomplete_variant_set(self) -> None:
        first_build = self.root / "first"
        second_build = self.root / "second"
        _write_build(first_build, "none")
        _write_build(second_build, "asan")
        _result, first = self._capture(first_build, "ci", "none")
        _result, second = self._capture(second_build, "ci-asan", "asan")

        corrupt = json.loads(second.read_text(encoding="utf-8"))
        corrupt["selection"]["digest"] = "0" * 64
        second.write_text(json.dumps(corrupt), encoding="utf-8")
        result = self._run(
            "compare",
            "--report",
            str(first),
            "--report",
            str(second),
            "--output",
            str(self.root / "corrupt.json"),
        )
        self.assertEqual(result.returncode, 3)
        self.assertIn("digest does not match", result.stderr)

        _result, second = self._capture(
            second_build, "ci-asan", "asan", output_name="asan-restored.json"
        )
        result = self._run(
            "compare",
            "--report",
            str(first),
            "--report",
            str(second),
            "--require-sanitizer",
            "none",
            "--require-sanitizer",
            "asan",
            "--require-sanitizer",
            "ubsan",
            "--output",
            str(self.root / "incomplete.json"),
        )
        self.assertEqual(result.returncode, 3)
        self.assertIn("does not match required variants", result.stderr)


if __name__ == "__main__":
    unittest.main()
