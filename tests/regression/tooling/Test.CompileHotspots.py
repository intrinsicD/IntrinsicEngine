#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools" / "analysis"))

import compile_hotspots  # noqa: E402
import benchmark_compile_iteration as bench  # noqa: E402


class CompileHotspotFixture:
    def __init__(self, root: Path, log_version: int = 5) -> None:
        self.root = root
        self.build = root / "build" / "ci"
        self.log_version = log_version
        self.build.mkdir(parents=True)
        for source_root in compile_hotspots.SOURCE_ROOTS:
            (root / source_root).mkdir()
        self.commands: list[dict[str, str]] = []
        self.log_lines = [f"# ninja log v{log_version}"]

    def log_command_field(self, identity: str) -> str:
        if self.log_version == 4:
            return identity
        return hashlib.sha256(identity.encode()).hexdigest()[:16]

    def source(self, relative_path: str, contents: str = "int value = 0;\n") -> Path:
        path = self.root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def command(self, source: Path, output: str) -> None:
        self.commands.append(
            {
                "directory": str(self.build),
                "command": f"clang++ -c {source} -o {output}",
                "file": str(source),
                "output": output,
            }
        )

    def record(
        self,
        start_ms: int,
        end_ms: int,
        output: str,
        command_hash: str,
        restat_mtime: int = 0,
    ) -> None:
        command_field = self.log_command_field(command_hash)
        self.log_lines.append(
            f"{start_ms}\t{end_ms}\t{restat_mtime}\t{output}\t{command_field}"
        )

    def write(self) -> None:
        (self.build / "compile_commands.json").write_text(
            json.dumps(self.commands),
            encoding="utf-8",
        )
        (self.build / ".ninja_log").write_text(
            "\n".join(self.log_lines) + "\n",
            encoding="utf-8",
        )

    def analyze(self) -> dict[str, object]:
        self.write()
        return compile_hotspots.analyze_build(self.root, self.build)


class ModuleBoundaryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.fixture = CompileHotspotFixture(Path(self.temporary.name))
        self.source = self.fixture.source("src/runtime/Spacing.cpp", "module Family;\n")
        self.output = "src/runtime/CMakeFiles/Runtime.dir/Spacing.cpp.o"
        self.fixture.command(self.source, self.output)
        self.fixture.write()
        self.object = self.fixture.build / self.output
        self.object.parent.mkdir(parents=True)
        self.object.write_text("")
        self.scan = Path(str(self.object) + ".ddi")
        self.graph = self.object.parent / "CXXModules.json"
        self.scan.write_text(json.dumps({"rules": [{
            "primary-output": self.output, "requires": [{"logical-name": "Family"}]
        }]}))
        self.graph.write_text(json.dumps({
            "modules": {"Family": {}}, "references": {"Core": {}, "OtherFamily": {}},
            "usages": {"Family": ["Core"]}
        }))

    def check(self) -> list[str]:
        return compile_hotspots.check_module_boundary(
            self.fixture.root, self.fixture.build, ["src/runtime/Spacing.cpp"], ["OtherFamily"]
        )

    def test_implicit_primary_and_transitive_import_are_checked(self) -> None:
        self.assertEqual(self.check(), [])
        graph = json.loads(self.graph.read_text())
        graph["usages"]["Core"] = ["OtherFamily"]
        self.graph.write_text(json.dumps(graph))
        self.assertEqual(self.check(), ["src/runtime/Spacing.cpp -> Family -> Core -> OtherFamily"])

    def test_missing_or_stale_scan_and_missing_producer_fail_closed(self) -> None:
        import os
        producer = self.fixture.build / self.output
        producer.unlink()
        with self.assertRaisesRegex(compile_hotspots.AnalysisError, "producer missing"):
            self.check()
        producer.touch()
        os.utime(self.source, ns=(self.scan.stat().st_mtime_ns + 1000000000,) * 2)
        with self.assertRaisesRegex(compile_hotspots.AnalysisError, "source newer"):
            self.check()
        self.scan.unlink()
        with self.assertRaisesRegex(compile_hotspots.AnalysisError, "metadata"):
            self.check()

    def test_missing_unresolved_and_ambiguous_dependencies_fail_closed(self) -> None:
        graph = json.loads(self.graph.read_text())
        graph["usages"]["Core"] = ["Missing"]
        self.graph.write_text(json.dumps(graph))
        with self.assertRaisesRegex(compile_hotspots.AnalysisError, "unresolved scanned module"):
            self.check()
        self.fixture.command(self.source, self.output + ".duplicate")
        self.fixture.write()
        with self.assertRaisesRegex(compile_hotspots.AnalysisError, "ambiguous compile producer"):
            self.check()

    def test_cycles_terminate_without_hiding_forbidden_modules(self) -> None:
        graph = json.loads(self.graph.read_text())
        graph["usages"]["Core"] = ["Family", "OtherFamily"]
        self.graph.write_text(json.dumps(graph))
        self.assertEqual(len(self.check()), 1)

    def test_malformed_and_mismatched_scanner_output_fail_closed(self) -> None:
        for scan in ({"rules": []}, {"rules": [{"primary-output": "wrong.o"}]}):
            with self.subTest(scan=scan):
                self.scan.write_text(json.dumps(scan))
                with self.assertRaises(compile_hotspots.AnalysisError):
                    self.check()


class CompileHotspotTests(unittest.TestCase):
    def test_multi_output_module_command_is_one_physical_edge(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            source = fixture.source(
                "src/core/Core.Alpha.cppm",
                "#include <cstdint>\nexport module Core.Alpha;\nimport Core.Beta;\n",
            )
            object_output = "src/core/CMakeFiles/Core.dir/Core.Alpha.cppm.o"
            pcm_output = "src/core/CMakeFiles/Core.dir/Core.Alpha.pcm"
            fixture.command(source, object_output)
            fixture.record(
                10,
                110,
                object_output,
                "same-command",
                restat_mtime=101,
            )
            fixture.record(
                10,
                110,
                pcm_output,
                "same-command",
                restat_mtime=202,
            )

            report = fixture.analyze()

            summary = report["summary"]
            self.assertEqual(summary["latest_compile_output_count"], 2)
            self.assertEqual(summary["physical_compile_edge_count"], 1)
            self.assertEqual(summary["multi_output_edge_count"], 1)
            edge = report["edges"][0]
            self.assertEqual(edge["duration_ms"], 100)
            self.assertEqual(edge["edge_kind"], "module-interface")
            self.assertEqual(
                edge["outputs"],
                sorted([object_output, pcm_output]),
            )
            self.assertEqual(edge["source"], "src/core/Core.Alpha.cppm")
            self.assertEqual(edge["resolution"]["status"], "resolved")
            self.assertEqual(len(edge["edge_id"]), 64)
            self.assertEqual(edge["source_lines"], 3)
            self.assertEqual(edge["includes"], 1)
            self.assertEqual(edge["imports"], 1)
            self.assertEqual(edge["exports"], 1)
            self.assertEqual(
                compile_hotspots.compare_baseline(
                    report["edges"],
                    {
                        "max_regression_ms": 0,
                        "targets": [
                            {
                                "source": "src/core/Core.Alpha.cppm",
                                "max_duration_ms": 100,
                            }
                        ],
                    },
                ),
                [],
            )

    def test_supported_ninja_log_versions_share_five_field_layout(self) -> None:
        for version in (4, 5, 6, 7):
            with self.subTest(version=version):
                with tempfile.TemporaryDirectory() as temporary:
                    fixture = CompileHotspotFixture(
                        Path(temporary),
                        log_version=version,
                    )
                    source = fixture.source("src/core/Core.Version.cpp")
                    output = "src/core/CMakeFiles/Core.dir/Core.Version.cpp.o"
                    fixture.command(source, output)
                    command_field = (
                        "clang++\t-c Core.Version.cpp" if version == 4 else "abc123"
                    )
                    fixture.record(0, 5, output, command_field)

                    edge = fixture.analyze()["edges"][0]

                    self.assertEqual(edge["source"], "src/core/Core.Version.cpp")
                    self.assertEqual(
                        edge["physical_identity"]["ninja_log_version"],
                        version,
                    )
                    if version == 4:
                        self.assertNotEqual(
                            edge["physical_identity"]["command_hash"],
                            command_field,
                        )

    def test_ninja_log_v7_groups_multi_output_hash_records(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary), log_version=7)
            source = fixture.source("src/core/Core.Version7.cppm")
            object_output = "src/core/CMakeFiles/Core.dir/Core.Version7.cppm.o"
            pcm_output = "src/core/CMakeFiles/Core.dir/Core.Version7.pcm"
            fixture.command(source, object_output)
            fixture.record(10, 40, object_output, "v7-command")
            fixture.record(10, 40, pcm_output, "v7-command")

            edge = fixture.analyze()["edges"][0]

            self.assertEqual(edge["outputs"], sorted([object_output, pcm_output]))
            self.assertEqual(
                edge["physical_identity"]["command_hash"],
                fixture.log_command_field("v7-command"),
            )
            self.assertEqual(edge["physical_identity"]["ninja_log_version"], 7)

    def test_distinct_phases_for_one_source_keep_distinct_edge_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            source = fixture.source("src/core/Core.Alpha.cppm")
            pcm_output = "src/core/CMakeFiles/Core.dir/Core.Alpha.pcm"
            object_output = "src/core/CMakeFiles/Core.dir/Core.Alpha.cppm.o"
            fixture.command(source, pcm_output)
            fixture.command(source, object_output)
            fixture.record(5, 25, pcm_output, "bmi-command")
            fixture.record(5, 25, object_output, "object-command")

            report = fixture.analyze()

            edges = report["edges"]
            self.assertEqual(len(edges), 2)
            self.assertEqual(
                {edge["source"] for edge in edges},
                {"src/core/Core.Alpha.cppm"},
            )
            self.assertEqual(len({edge["edge_id"] for edge in edges}), 2)
            failures = compile_hotspots.compare_baseline(
                edges,
                {
                    "max_regression_ms": 0,
                    "targets": [
                        {
                            "source": "src/core/Core.Alpha.cppm",
                            "max_duration_ms": 100,
                        }
                    ],
                },
            )
            self.assertEqual(len(failures), 1)
            self.assertIn("ambiguous source", failures[0])

            selected = edges[0]
            self.assertEqual(
                compile_hotspots.compare_baseline(
                    edges,
                    {
                        "max_regression_ms": 0,
                        "targets": [
                            {
                                "edge_id": selected["edge_id"],
                                "source": selected["source"],
                                "edge_kind": selected["edge_kind"],
                                "outputs": selected["outputs"],
                                "max_duration_ms": selected["duration_ms"],
                            }
                        ],
                    },
                ),
                [],
            )

    def test_repository_baseline_edge_ids_match_declared_identity(self) -> None:
        baseline_path = (
            REPO_ROOT / "tools" / "analysis" / "compile_hotspot_baseline.json"
        )
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))

        for target in baseline["targets"]:
            with self.subTest(source=target["source"]):
                edge_identity = json.dumps(
                    [target["source"], target["edge_kind"], target["outputs"]],
                    separators=(",", ":"),
                )
                self.assertEqual(
                    target["edge_id"],
                    hashlib.sha256(edge_identity.encode()).hexdigest(),
                )

    def test_exact_compile_commands_resolve_all_declared_roots(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            expected: dict[str, str] = {}
            for index, source_root in enumerate(
                compile_hotspots.SOURCE_ROOTS,
                start=1,
            ):
                relative = f"{source_root}/nested/Shared.cpp"
                source = fixture.source(relative)
                output = (
                    f"{source_root}/CMakeFiles/Owner{index}.dir/nested/Shared.cpp.o"
                )
                fixture.command(source, output)
                fixture.record(index * 10, index * 10 + 5, output, f"hash-{index}")
                expected[output] = relative

            report = fixture.analyze()

            self.assertEqual(
                {edge["output"]: edge["source"] for edge in report["edges"]},
                expected,
            )
            roots = {root["root"]: root for root in report["source_roots"]}
            for source_root in compile_hotspots.SOURCE_ROOTS:
                with self.subTest(source_root=source_root):
                    self.assertEqual(
                        roots[source_root]["configured_command_count"],
                        1,
                    )
                    self.assertEqual(
                        roots[source_root]["compiled_edge_count"],
                        1,
                    )
                    self.assertTrue(roots[source_root]["present_in_sampled_build"])

    def test_generated_outside_root_and_unresolved_outputs_are_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            fixture.source("tests/a/Duplicate.cpp")
            fixture.source("methods/b/Duplicate.cpp")
            generated = fixture.source("build/ci/generated/Generated.cpp")
            outside_declared = fixture.source("tools/analysis/OwnedHelper.cpp")
            generated_output = "CMakeFiles/Generated.dir/generated/Generated.cpp.o"
            outside_output = "CMakeFiles/Helper.dir/tools/analysis/OwnedHelper.cpp.o"
            unresolved_output = "CMakeFiles/Missing.dir/Duplicate.cpp.o"
            fixture.command(generated, generated_output)
            fixture.command(outside_declared, outside_output)
            fixture.record(0, 5, generated_output, "generated")
            fixture.record(5, 12, outside_output, "outside")
            fixture.record(12, 21, unresolved_output, "unresolved")

            report = fixture.analyze()

            by_output = {edge["output"]: edge for edge in report["edges"]}
            self.assertEqual(
                by_output[generated_output]["resolution"]["status"],
                "generated",
            )
            self.assertEqual(
                by_output[outside_output]["resolution"]["status"],
                "outside-declared-roots",
            )
            unresolved = by_output[unresolved_output]
            self.assertEqual(
                unresolved["resolution"]["reason"],
                "output-missing-from-current-compile-commands",
            )
            self.assertEqual(
                unresolved["resolution"]["candidates"],
                [
                    "methods/b/Duplicate.cpp",
                    "tests/a/Duplicate.cpp",
                ],
            )
            self.assertEqual(report["summary"]["unresolved_edge_count"], 1)
            self.assertEqual(report["summary"]["unresolved_output_count"], 1)
            self.assertEqual(
                report["summary"]["outside_declared_roots_edge_count"],
                1,
            )
            self.assertEqual(
                report["summary"]["resolution_issue_output_count"],
                2,
            )
            self.assertEqual(len(report["resolution_issues"]), 2)

    def test_repository_dependency_roots_are_excluded_from_rankings(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            expected_sources = {
                "external/vendor/External.cpp",
                "third_party/vendor/ThirdParty.cpp",
            }
            for index, relative in enumerate(sorted(expected_sources), start=1):
                source = fixture.source(relative)
                output = f"CMakeFiles/Dependency{index}.dir/{source.name}.o"
                fixture.command(source, output)
                fixture.record(index, index + 5, output, f"dependency-{index}")

            report = fixture.analyze()

            self.assertEqual(
                {edge["source"] for edge in report["edges"]},
                expected_sources,
            )
            self.assertEqual(
                {edge["resolution"]["status"] for edge in report["edges"]},
                {"dependency"},
            )
            self.assertEqual(report["summary"]["dependency_edge_count"], 2)
            self.assertEqual(report["resolution_issues"], [])
            self.assertEqual(
                compile_hotspots._repository_owned_rows(report["edges"]),
                [],
            )

    def test_compile_command_output_with_two_sources_is_ambiguous(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            first = fixture.source("tests/a/Shared.cpp")
            second = fixture.source("methods/b/Shared.cpp")
            output = "CMakeFiles/Shared.dir/Shared.cpp.o"
            fixture.command(first, output)
            fixture.command(second, output)
            fixture.record(0, 10, output, "ambiguous")

            report = fixture.analyze()

            edge = report["edges"][0]
            self.assertEqual(edge["resolution"]["status"], "ambiguous")
            self.assertEqual(
                edge["resolution"]["candidates"],
                ["methods/b/Shared.cpp", "tests/a/Shared.cpp"],
            )
            self.assertEqual(report["summary"]["ambiguous_edge_count"], 1)
            failures = compile_hotspots.compare_baseline(
                [edge],
                {
                    "max_regression_ms": 0,
                    "targets": [
                        {
                            "edge_id": edge["edge_id"],
                            "max_duration_ms": edge["duration_ms"],
                        }
                    ],
                },
            )
            self.assertEqual(len(failures), 1)
            self.assertIn("not baseline-eligible", failures[0])
            self.assertIn("'ambiguous'", failures[0])

    def test_cli_baseline_reports_ambiguous_edge_as_ineligible(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            first = fixture.source("tests/a/Shared.cpp")
            second = fixture.source("methods/b/Shared.cpp")
            output = "CMakeFiles/Shared.dir/Shared.cpp.o"
            fixture.command(first, output)
            fixture.command(second, output)
            fixture.record(0, 10, output, "ambiguous-cli")
            report = fixture.analyze()
            edge = report["edges"][0]
            baseline = fixture.root / "baseline.json"
            baseline.write_text(
                json.dumps(
                    {
                        "max_regression_ms": 0,
                        "targets": [
                            {
                                "edge_id": edge["edge_id"],
                                "max_duration_ms": edge["duration_ms"],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            output_text = io.StringIO()

            with (
                mock.patch.object(
                    compile_hotspots,
                    "analyze_build",
                    return_value=report,
                ),
                contextlib.redirect_stdout(output_text),
            ):
                result = compile_hotspots.main(
                    [
                        "--build-dir",
                        str(fixture.build),
                        "--baseline-json",
                        str(baseline),
                    ]
                )

            self.assertEqual(result, 2)
            diagnostic = output_text.getvalue()
            self.assertIn("not baseline-eligible", diagnostic)
            self.assertIn("'ambiguous'", diagnostic)
            self.assertNotIn("missing compile edge_id", diagnostic)

    def test_missing_resolved_source_has_unresolved_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            source = fixture.source("tests/Test.Missing.cpp")
            output = "tests/CMakeFiles/Missing.dir/Test.Missing.cpp.o"
            fixture.command(source, output)
            fixture.record(0, 10, output, "missing-source")
            source.unlink()

            missing = fixture.analyze()["edges"][0]

            self.assertEqual(missing["resolution"]["status"], "unresolved")
            self.assertEqual(
                missing["resolution"]["reason"],
                "resolved-repository-source-is-missing",
            )
            self.assertEqual(missing["edge_kind"], "unresolved")
            failures = compile_hotspots.compare_baseline(
                [missing],
                {
                    "max_regression_ms": 0,
                    "targets": [
                        {
                            "edge_id": missing["edge_id"],
                            "max_duration_ms": missing["duration_ms"],
                        }
                    ],
                },
            )
            self.assertEqual(len(failures), 1)
            self.assertIn("'unresolved'", failures[0])

            fixture.source("tests/Test.Missing.cpp")
            resolved = fixture.analyze()["edges"][0]
            self.assertEqual(resolved["resolution"]["status"], "resolved")
            self.assertEqual(resolved["edge_kind"], "translation-unit")
            self.assertNotEqual(missing["edge_id"], resolved["edge_id"])

    def test_latest_output_record_wins_before_physical_grouping(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            source = fixture.source("tests/Test.Latest.cpp")
            output = "tests/CMakeFiles/Latest.dir/Test.Latest.cpp.o"
            fixture.command(source, output)
            fixture.record(0, 100, output, "old-command")
            fixture.record(200, 225, output, "new-command")

            report = fixture.analyze()

            self.assertEqual(report["summary"]["latest_compile_output_count"], 1)
            self.assertEqual(report["summary"]["physical_compile_edge_count"], 1)
            edge = report["edges"][0]
            self.assertEqual(edge["duration_ms"], 25)
            self.assertEqual(
                edge["physical_identity"]["command_hash"],
                fixture.log_command_field("new-command"),
            )

    def test_malformed_or_unsupported_ninja_log_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            log = build / ".ninja_log"
            log.write_text("# ninja log vx\n", encoding="utf-8")
            with self.assertRaisesRegex(
                compile_hotspots.AnalysisError,
                "malformed Ninja log header",
            ):
                compile_hotspots.parse_ninja_log(log, build)

            for version in (3, 8):
                with self.subTest(version=version):
                    log.write_text(
                        f"# ninja log v{version}\n",
                        encoding="utf-8",
                    )
                    with self.assertRaisesRegex(
                        compile_hotspots.AnalysisError,
                        "unsupported Ninja log version",
                    ):
                        compile_hotspots.parse_ninja_log(log, build)

            log.write_text(
                "# ninja log v5\nnot-enough-fields\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                compile_hotspots.AnalysisError,
                "expected five",
            ):
                compile_hotspots.parse_ninja_log(log, build)

            log.write_text(
                "# ninja log v5\n0\t5\t0\tCore.Version.cpp.o\tnot-hex\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                compile_hotspots.AnalysisError,
                "command hash.*lowercase hexadecimal",
            ):
                compile_hotspots.parse_ninja_log(log, build)

            log.write_text(
                "# ninja log v5\n0\t5\t0\tCore.Version.cpp.o\tabc123\textra\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                compile_hotspots.AnalysisError,
                "expected five",
            ):
                compile_hotspots.parse_ninja_log(log, build)

    def test_baseline_rejects_empty_target_set(self) -> None:
        with self.assertRaisesRegex(
            compile_hotspots.AnalysisError,
            "at least one target",
        ):
            compile_hotspots.compare_baseline(
                [],
                {
                    "max_regression_ms": 0,
                    "targets": [],
                },
            )

    def test_baseline_schema_rejects_coercions_and_field_drift(self) -> None:
        edge_id = "a" * 64
        valid_target = {
            "edge_id": edge_id,
            "max_duration_ms": 10,
        }
        cases = [
            (
                "missing-root-field",
                {"targets": [valid_target]},
                "omits required fields",
            ),
            (
                "unknown-root-field",
                {
                    "max_regression_ms": 0,
                    "targets": [valid_target],
                    "unknown": 0,
                },
                "unknown fields",
            ),
            *[
                (
                    f"tolerance-{type(value).__name__}",
                    {
                        "max_regression_ms": value,
                        "targets": [valid_target],
                    },
                    "nonnegative integer",
                )
                for value in (True, 1.5, "1", -1)
            ],
            (
                "missing-target-duration",
                {
                    "max_regression_ms": 0,
                    "targets": [{"edge_id": edge_id}],
                },
                "omits required fields",
            ),
            (
                "unknown-target-field",
                {
                    "max_regression_ms": 0,
                    "targets": [{**valid_target, "unknown": 0}],
                },
                "unknown fields",
            ),
            *[
                (
                    f"duration-{type(value).__name__}",
                    {
                        "max_regression_ms": 0,
                        "targets": [
                            {
                                "edge_id": edge_id,
                                "max_duration_ms": value,
                            }
                        ],
                    },
                    "nonnegative integer",
                )
                for value in (True, 1.5, "1", -1)
            ],
            (
                "missing-identity",
                {
                    "max_regression_ms": 0,
                    "targets": [{"max_duration_ms": 10}],
                },
                "requires edge_id or source",
            ),
            (
                "malformed-edge-id",
                {
                    "max_regression_ms": 0,
                    "targets": [
                        {
                            "edge_id": "not-an-edge-id",
                            "max_duration_ms": 10,
                        }
                    ],
                },
                "64 lowercase hexadecimal",
            ),
            *[
                (
                    f"source-{type(value).__name__}",
                    {
                        "max_regression_ms": 0,
                        "targets": [
                            {
                                "edge_id": edge_id,
                                "source": value,
                                "max_duration_ms": 10,
                            }
                        ],
                    },
                    "source must be a non-empty string",
                )
                for value in (None, "", 7)
            ],
            (
                "invalid-edge-kind",
                {
                    "max_regression_ms": 0,
                    "targets": [{**valid_target, "edge_kind": "unresolved"}],
                },
                "edge_kind must be one of",
            ),
            (
                "invalid-edge-kind-type",
                {
                    "max_regression_ms": 0,
                    "targets": [{**valid_target, "edge_kind": []}],
                },
                "edge_kind must be one of",
            ),
            (
                "invalid-outputs",
                {
                    "max_regression_ms": 0,
                    "targets": [{**valid_target, "outputs": "output.o"}],
                },
                "non-empty array of strings",
            ),
        ]

        for name, baseline, expected in cases:
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                    compile_hotspots.AnalysisError,
                    expected,
                ):
                    compile_hotspots.compare_baseline([], baseline)

    def test_baseline_rejects_missing_duplicate_and_regressed_edges(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            source = fixture.source("benchmarks/Bench_Alpha.cpp")
            output = "benchmarks/CMakeFiles/Bench.dir/Bench_Alpha.cpp.o"
            fixture.command(source, output)
            fixture.record(0, 50, output, "benchmark")
            edge = fixture.analyze()["edges"][0]

            failures = compile_hotspots.compare_baseline(
                [edge],
                {
                    "max_regression_ms": 2,
                    "targets": [
                        {
                            "edge_id": edge["edge_id"],
                            "max_duration_ms": 40,
                        },
                        {
                            "edge_id": edge["edge_id"],
                            "max_duration_ms": 50,
                        },
                        {
                            "edge_id": "0" * 64,
                            "max_duration_ms": 50,
                        },
                    ],
                },
            )
            self.assertEqual(len(failures), 3)
            self.assertIn("exceeds budget", failures[0])
            self.assertIn("duplicate baseline target", failures[1])
            self.assertIn("missing compile edge_id", failures[2])

    def test_baseline_rejects_mixed_selectors_for_one_physical_edge(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = CompileHotspotFixture(Path(temporary))
            source = fixture.source("benchmarks/Bench_Alpha.cpp")
            output = "benchmarks/CMakeFiles/Bench.dir/Bench_Alpha.cpp.o"
            fixture.command(source, output)
            fixture.record(0, 50, output, "benchmark")
            edge = fixture.analyze()["edges"][0]
            selectors = [
                {"edge_id": edge["edge_id"], "max_duration_ms": 50},
                {"source": edge["source"], "max_duration_ms": 50},
            ]

            for targets in (selectors, list(reversed(selectors))):
                with self.subTest(first_selector=next(iter(targets[0]))):
                    failures = compile_hotspots.compare_baseline(
                        [edge],
                        {
                            "max_regression_ms": 0,
                            "targets": targets,
                        },
                    )

                    self.assertEqual(len(failures), 1)
                    self.assertIn("duplicate baseline physical edge", failures[0])
                    self.assertIn(edge["edge_id"], failures[0])
                    self.assertIn("resolves the same edge as", failures[0])


class CompileIterationMeasurementTests(unittest.TestCase):
    def test_dependency_snapshot_tracks_content_paths_and_membership(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "Preinstalled dependencies are missing"):
                bench.dependency_snapshot(root)
            header = root / "header.h"
            header.write_bytes(b"before")
            original = bench.dependency_snapshot(root)
            self.assertEqual(original, bench.dependency_snapshot(root))
            self.assertEqual(original["file_count"], 1)
            header.write_bytes(b"change")  # Same size still invalidates the snapshot.
            self.assertNotEqual(original, bench.dependency_snapshot(root))
            header.write_bytes(b"before")
            renamed = root / "renamed.h"
            header.rename(renamed)
            self.assertNotEqual(original, bench.dependency_snapshot(root))
            renamed.rename(header)
            self.assertEqual(original, bench.dependency_snapshot(root))
            (root / "new.h").touch()
            self.assertEqual(bench.dependency_snapshot(root)["file_count"], 2)
            self.assertNotEqual(original, bench.dependency_snapshot(root))

    def test_dependency_snapshot_tracks_links_and_permissions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first, second, link = root / "first", root / "second", root / "link"
            first.write_bytes(b"same")
            second.write_bytes(b"same")
            link.symlink_to(first.name)
            original = bench.dependency_snapshot(root)
            link.unlink()
            link.symlink_to(second.name)
            self.assertNotEqual(original, bench.dependency_snapshot(root))
            link.unlink()
            link.symlink_to(first.name)
            self.assertEqual(original, bench.dependency_snapshot(root))
            first.chmod(first.stat().st_mode ^ 0o100)
            self.assertNotEqual(original, bench.dependency_snapshot(root))
            link.unlink()
            without_link = bench.dependency_snapshot(root)
            link.symlink_to("missing")
            self.assertNotEqual(without_link, bench.dependency_snapshot(root))
            link.unlink()
            link.symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "Unsupported dependency directory symlink"):
                bench.dependency_snapshot(root)

    def test_log_window_excludes_old_commands_and_rejects_compaction(self):
        old = b"# ninja log v5\n0\t20\t0\told.o\ta\n"
        new = b"20\t30\t0\tnew.o\tb\n"
        self.assertEqual(bench.log_window(old, old + new), b"# ninja log v5\n" + new)
        self.assertEqual(bench.log_window(old, old), b"# ninja log v5\n")
        with self.assertRaises(ValueError):
            bench.log_window(old, b"# ninja log v5\n" + new)

    def test_parallel_work_is_not_added_to_the_dependency_critical_path(self):
        graph = '''digraph ninja {
"a" [label="a.o"]
"b" [label="b.o"]
"r" [label="LINK", shape=ellipse]
"out" [label="lib.a"]
"a" -> "r"
"b" -> "r"
"r" -> "out"
}'''
        log = b"# ninja log v5\n0\t20\t0\ta.o\ta\n0\t30\t0\tb.o\tb\n30\t35\t0\tlib.a\tc\n"
        # Extra multi-output sinks may be unlabelled in Ninja's target subgraph.
        graph = graph.replace('"r" -> "out"', '"r" -> "out"\n"r" -> "ancillary"')
        result = bench.critical_path(graph, log)
        self.assertEqual(result["duration_ms"], 35)
        self.assertEqual([p["output"] for p in result["timed_path"]], ["b.o", "lib.a"])
        # CMake's glob check is build-system overhead outside the target DAG.
        for output in ("CMakeFiles/cmake.verify_globs",
                       "/tmp/build/CMakeFiles/cmake.verify_globs"):
            checked = bench.critical_path(graph, log + f"0\t7\t0\t{output}\tglob\n".encode())
            self.assertEqual(checked["duration_ms"], 35)
            self.assertEqual(checked["unmapped_meta_outputs"], [output])
        with self.assertRaises(ValueError):
            bench.critical_path(graph, log + b"0\t7\t0\tother/cmake.verify_globs\tglob\n")
        with self.assertRaises(ValueError):
            bench.critical_path(graph, log + b"0\t100\t0\tmissing.o\td\n")
        with self.assertRaises(ValueError):
            bench.critical_path(graph.replace('"r" -> "out"', '"r" -> "out"\n"out" -> "a"'), log)



class CompileIterationProbeTests(unittest.TestCase):
    def test_header_requires_every_declared_consumer(self):
        params = {"probe_sources": {"header": ["a.cpp", "b.cpp"]}}
        rows = [{"source": name} for name in ("a.cpp", "b.cpp", "extra.cpp")]
        bench.validate_probe_sources(params, "header", "shared.hpp", rows)
        for missing in ("a.cpp", "b.cpp"):
            with self.subTest(missing=missing), self.assertRaisesRegex(
                AssertionError, "declared target sources"
            ):
                bench.validate_probe_sources(
                    params, "header", "shared.hpp",
                    [row for row in rows if row["source"] != missing],
                )

    def test_header_without_consumers_fails_closed(self):
        for params in ({}, {"probe_sources": {"header": []}}):
            with self.subTest(params=params), self.assertRaisesRegex(
                AssertionError, "declared target sources"
            ):
                bench.validate_probe_sources(
                    params, "header", "shared.hpp", [{"source": "a.cpp"}]
                )

    def test_relocated_consumers_use_the_measured_arm_and_fail_closed(self):
        params = {"probe_sources": {"header": {
            "before": ["one.cpp", "two.cpp"], "after": ["shared.cpp"]}}}
        bench.validate_probe_sources(params, "header", "shared.hpp",
                                     [{"source": "shared.cpp"}], arm="after")
        with self.assertRaises(AssertionError):
            bench.validate_probe_sources(params, "header", "shared.hpp",
                                         [{"source": "one.cpp"}], arm="before")
        with self.assertRaises(AssertionError):
            bench.validate_probe_sources(params, "header", "shared.hpp",
                                         [{"source": "shared.cpp"}], arm="before")
        with self.assertRaises(KeyError):
            bench.validate_probe_sources(params, "header", "shared.hpp", [], arm="missing")

    def test_source_probe_defaults_to_touched_source(self):
        bench.validate_probe_sources({}, "edit", "a.cpp", [{"source": "a.cpp"}])
        with self.assertRaisesRegex(AssertionError, "declared target sources"):
            bench.validate_probe_sources({}, "edit", "a.cpp", [{"source": "b.cpp"}])


if __name__ == "__main__":
    unittest.main()
