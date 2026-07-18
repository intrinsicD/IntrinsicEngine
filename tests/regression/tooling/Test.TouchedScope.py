#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT_PATH = REPO_ROOT / "tools" / "ci" / "touched_scope.py"

spec = importlib.util.spec_from_file_location("touched_scope", SCRIPT_PATH)
assert spec is not None and spec.loader is not None
touched_scope = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = touched_scope
spec.loader.exec_module(touched_scope)


def record(path: str, status: str = "M") -> object:
    return touched_scope.ChangeRecord(status=status, path=path)


def write_registry(
    build_dir: Path,
    rows: list[tuple[str, str]] | None = None,
    *,
    fast: list[str] | None = None,
    smoke: list[str] | None = None,
) -> None:
    inventory = build_dir / "test-inventories"
    inventory.mkdir(parents=True)
    rows = rows or [
        ("IntrinsicGeometryTests", "geometry,unit"),
        ("IntrinsicGeometryIoTests", "geometry,unit"),
        ("GeometryContractTests", "contract,geometry"),
        ("IntrinsicRuntimeGraphicsCpuTests", "graphics,integration,runtime"),
    ]
    if fast is None:
        fast = [
            target
            for target, labels in rows
            if {"unit", "contract"}.intersection(labels.split(","))
            and not set(touched_scope.DEFAULT_EXCLUDE_LABELS).intersection(
                labels.split(",")
            )
        ]
    if smoke is None:
        smoke = [
            target
            for target, labels in rows
            if {"graphics", "integration", "runtime"}.issubset(labels.split(","))
            and not set(touched_scope.DEFAULT_EXCLUDE_LABELS).intersection(
                labels.split(",")
            )
        ]
    (inventory / "RegisteredTestTargets.tsv").write_text(
        "target\tlabels\n"
        + "".join(f"{target}\t{labels}\n" for target, labels in rows),
        encoding="utf-8",
    )
    (inventory / "IntrinsicPrFastTests.txt").write_text(
        "".join(f"{target}\n" for target in fast),
        encoding="utf-8",
    )
    (inventory / "IntrinsicPrSmokeTests.txt").write_text(
        "".join(f"{target}\n" for target in smoke),
        encoding="utf-8",
    )


class TouchedScopeTests(unittest.TestCase):
    def test_hidden_directory_normalization_is_preserved(self) -> None:
        self.assertEqual(
            touched_scope.normalize_changed_path(".github/workflows/pr-fast.yml"),
            ".github/workflows/pr-fast.yml",
        )
        self.assertEqual(
            touched_scope.normalize_changed_path("./.github/workflows/pr-fast.yml"),
            ".github/workflows/pr-fast.yml",
        )
        self.assertEqual(
            touched_scope.normalize_changed_path(" docs/file.md "),
            " docs/file.md ",
        )
        for path in ("../outside", "docs//file.md", "docs/./file.md"):
            with self.subTest(path=path):
                with self.assertRaises(touched_scope.DiffError):
                    touched_scope.normalize_changed_path(path)

    def test_nul_name_status_parser_handles_modify_and_rename(self) -> None:
        records = touched_scope.parse_name_status_z(
            b"M\0src/geometry/Geometry.Mesh.cpp\0R100\0docs/old.md\0docs/new.md\0"
        )
        self.assertEqual(
            records,
            [
                touched_scope.ChangeRecord("M", "src/geometry/Geometry.Mesh.cpp"),
                touched_scope.ChangeRecord("R100", "docs/new.md", "docs/old.md"),
            ],
        )
        with self.assertRaises(touched_scope.DiffError):
            touched_scope.parse_name_status_z(b"M\0unterminated")
        with self.assertRaises(touched_scope.DiffError):
            touched_scope.parse_name_status_z(b"bogus\0path\0")
        whitespace = touched_scope.parse_name_status_z(b"M\0 docs/file.md \0")
        self.assertEqual(whitespace, [record(" docs/file.md ")])

    def test_docs_and_tasks_are_structural_only(self) -> None:
        docs = touched_scope.analyze_change_records(
            [record("docs/build-troubleshooting.md")]
        )
        self.assertEqual(docs["route"], "structural")
        self.assertFalse(docs["needs_cpp"])
        self.assertEqual(docs["structural_checks"], ["docs"])

        tasks = touched_scope.analyze_change_records([record("tasks/active/CI-005.md")])
        self.assertEqual(tasks["route"], "structural")
        self.assertFalse(tasks["needs_cpp"])
        self.assertEqual(
            tasks["structural_checks"],
            ["docs", "session_brief", "task_policy", "task_state"],
        )

    def test_known_single_and_cross_layer_sources_are_focused(self) -> None:
        geometry = touched_scope.analyze_change_records(
            [record("src/geometry/Geometry.Mesh.cpp")]
        )
        self.assertEqual(geometry["route"], "focused")
        self.assertEqual(geometry["owner_labels"], ["geometry"])
        self.assertIn("IntrinsicGeometryTests", geometry["anchor_targets"])
        self.assertIn("IntrinsicGeometryIoTests", geometry["anchor_targets"])

        geometry_io = touched_scope.analyze_change_records(
            [record("tests/unit/geometry/Test.GeometryIO.cpp")]
        )
        self.assertEqual(geometry_io["route"], "focused")
        self.assertEqual(geometry_io["owner_labels"], ["geometry"])
        self.assertEqual(
            geometry_io["anchor_targets"],
            ["IntrinsicGeometryIoTests"],
        )

        cross_layer = touched_scope.analyze_change_records(
            [
                record("src/assets/Asset.Service.cpp"),
                record("src/physics/Physics.World.cpp"),
            ]
        )
        self.assertEqual(cross_layer["route"], "focused")
        self.assertEqual(cross_layer["owner_labels"], ["assets", "physics"])
        self.assertIn("IntrinsicPhysicsWorldTests", cross_layer["anchor_targets"])

    def test_runtime_mapping_has_no_retired_selection_target(self) -> None:
        route = touched_scope.analyze_change_records(
            [record("src/runtime/Runtime.Engine.cpp")]
        )
        self.assertEqual(route["route"], "focused")
        self.assertIn("IntrinsicRuntimeContractTests", route["anchor_targets"])
        self.assertIn("IntrinsicRuntimeUnitTests", route["anchor_targets"])
        self.assertNotIn(
            "IntrinsicRuntimeSelectionContractTests", route["anchor_targets"]
        )

    def test_graph_affecting_and_unknown_paths_are_broad(self) -> None:
        paths = (
            "src/runtime/Runtime.Engine.cppm",
            "src/runtime/Runtime.Engine.hpp",
            "src/core/Core.Error.cpp",
            "src/graphics/vulkan/Vulkan.Device.cpp",
            "cmake/IntrinsicModule.cmake",
            "CMakePresets.json",
            "vcpkg.json",
            "methods/geometry/example.cpp",
            "unmapped/root.data",
        )
        for path in paths:
            with self.subTest(path=path):
                route = touched_scope.analyze_change_records([record(path)])
                self.assertEqual(route["route"], "broad")
                self.assertTrue(route["needs_cpp"])

    def test_workflow_change_is_structural_and_selects_static_regressions(
        self,
    ) -> None:
        route = touched_scope.analyze_change_records(
            [record(".github/workflows/pr-fast.yml")]
        )
        self.assertEqual(route["route"], "structural")
        self.assertIn("workflow_regression_tests", route["structural_checks"])
        commands = touched_scope.structural_commands(".", route["structural_checks"])
        text = "\n".join(command.shell_text() for command in commands)
        self.assertIn("Test.WorkflowConcurrency.py", text)
        self.assertIn("Test.WorkflowRouting.py", text)
        self.assertIn("Test.CcacheWorkflow.py", text)
        self.assertIn("Test.CiTiming.py", text)
        self.assertIn("check_workflow_names.py", text)

    def test_kernel_paths_select_live_and_synthetic_guards(self) -> None:
        route = touched_scope.analyze_change_records(
            [record("tools/repo/check_kernel_convergence.py")]
        )
        commands = touched_scope.structural_commands(".", route["structural_checks"])
        text = "\n".join(command.shell_text() for command in commands)
        self.assertIn("Test.CheckKernelConvergence.py", text)
        self.assertIn("check_kernel_convergence.py --root . --strict", text)

    def test_ci_tooling_selects_its_owning_regressions(self) -> None:
        ccache = touched_scope.analyze_change_records([record("tools/ci/ccache_ci.py")])
        ccache_text = "\n".join(
            command.shell_text()
            for command in touched_scope.structural_commands(
                ".",
                ccache["structural_checks"],
            )
        )
        self.assertIn("Test.CcacheWorkflow.py", ccache_text)
        self.assertNotIn("Test.TouchedScope.py", ccache_text)

        selection = touched_scope.analyze_change_records(
            [record("tools/ci/cpu_test_selection.py")]
        )
        selection_text = "\n".join(
            command.shell_text()
            for command in touched_scope.structural_commands(
                ".",
                selection["structural_checks"],
            )
        )
        self.assertIn("Test.CpuTestSelection.py", selection_text)
        self.assertIn("Test.SanitizerPresets.py", selection_text)
        self.assertNotIn("Test.TouchedScope.py", selection_text)

        timing = touched_scope.analyze_change_records(
            [record("tests/regression/tooling/Test.CiTiming.py")]
        )
        timing_text = "\n".join(
            command.shell_text()
            for command in touched_scope.structural_commands(
                ".",
                timing["structural_checks"],
            )
        )
        self.assertIn("Test.CiTiming.py", timing_text)
        self.assertNotIn("Test.TouchedScope.py", timing_text)

    def test_compile_hotspot_analyzer_selects_only_its_regression(self) -> None:
        route = touched_scope.analyze_change_records(
            [record("tools/analysis/compile_hotspots.py")]
        )

        self.assertEqual(route["route"], "structural")
        self.assertFalse(route["needs_cpp"])
        self.assertEqual(
            route["structural_checks"],
            ["tooling_test:Test.CompileHotspots.py"],
        )
        commands = touched_scope.structural_commands(
            ".",
            route["structural_checks"],
        )
        self.assertEqual(len(commands), 1)
        self.assertEqual(
            commands[0].shell_text(),
            "python3 tests/regression/tooling/Test.CompileHotspots.py",
        )

    def test_rename_delete_type_and_unmerged_statuses_broaden(self) -> None:
        for status in ("R100", "D", "T", "U"):
            with self.subTest(status=status):
                route = touched_scope.analyze_change_records(
                    [record("docs/file.md", status)]
                )
                self.assertEqual(route["route"], "broad")
                self.assertTrue(route["needs_cpp"])

    def test_zero_change_set_broadens_instead_of_succeeding_empty(self) -> None:
        route = touched_scope.analyze_change_records([])
        self.assertEqual(route["route"], "broad")
        self.assertTrue(route["needs_cpp"])
        self.assertEqual(route["reasons"][0]["code"], "zero-change-set")

    def test_real_git_diff_uses_two_endpoints_and_missing_ref_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            subprocess.run(
                ["git", "config", "user.email", "fixture@example.com"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                ["git", "config", "user.name", "Fixture"],
                cwd=root,
                check=True,
            )
            (root / "docs").mkdir()
            (root / "docs" / "file.md").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(["git", "commit", "-qm", "base"], cwd=root, check=True)
            base = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            (root / "docs" / "file.md").write_text("candidate\n", encoding="utf-8")
            subprocess.run(["git", "commit", "-qam", "candidate"], cwd=root, check=True)
            head = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            merge_base, records = touched_scope.collect_change_records(
                root,
                base,
                head,
            )
            self.assertEqual(merge_base, base)
            self.assertEqual(records, [record("docs/file.md")])
            with self.assertRaises(touched_scope.DiffError):
                touched_scope.collect_change_records(root, "missing", head)

    def test_diverged_pr_diff_starts_at_merge_base(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            subprocess.run(
                ["git", "config", "user.email", "fixture@example.com"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                ["git", "config", "user.name", "Fixture"],
                cwd=root,
                check=True,
            )
            (root / "README.md").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(["git", "commit", "-qm", "base"], cwd=root, check=True)
            base = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()

            subprocess.run(
                ["git", "switch", "-q", "-c", "feature"],
                cwd=root,
                check=True,
            )
            (root / "docs").mkdir()
            (root / "docs" / "feature.md").write_text(
                "feature\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "feature"],
                cwd=root,
                check=True,
            )
            feature = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()

            subprocess.run(
                ["git", "switch", "-q", "--detach", base],
                cwd=root,
                check=True,
            )
            (root / "src" / "core").mkdir(parents=True)
            (root / "src" / "core" / "Core.New.cpp").write_text(
                "main\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "main advance"],
                cwd=root,
                check=True,
            )
            main_tip = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()

            merge_base, records = touched_scope.collect_change_records(
                root,
                main_tip,
                feature,
            )
            self.assertEqual(merge_base, base)
            self.assertEqual(records, [record("docs/feature.md", "A")])

    def test_plan_action_turns_diff_error_into_broad_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "route.json"
            result = touched_scope.main(
                [
                    "--root",
                    str(root),
                    "--base-ref",
                    "missing",
                    "--head-ref",
                    "HEAD",
                    "--output",
                    str(output),
                ]
            )
            self.assertEqual(result, 0)
            route = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(route["route"], "broad")
            self.assertEqual(route["diff"]["status"], "error")

    def test_step_summary_names_scope_reasons_labels_and_targets(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "route.json"
            summary = root / "summary.md"
            build_dir = root / "build"
            write_registry(build_dir)
            result = touched_scope.main(
                [
                    "--root",
                    str(REPO_ROOT),
                    "--changed-file",
                    "src/geometry/Geometry.Mesh.cpp",
                    "--output",
                    str(output),
                    "--step-summary",
                    str(summary),
                ]
            )
            self.assertEqual(result, 0)
            result = touched_scope.main(
                [
                    "--root",
                    str(REPO_ROOT),
                    "--action",
                    "finalize",
                    "--plan",
                    str(output),
                    "--build-dir",
                    str(build_dir),
                    "--step-summary",
                    str(summary),
                ]
            )
            self.assertEqual(result, 0)
            text = summary.read_text(encoding="utf-8")
            self.assertIn("src/geometry/Geometry.Mesh.cpp", text)
            self.assertIn("known-owner", text)
            self.assertIn("geometry", text)
            self.assertIn("broad fallback: `false`", text)
            self.assertIn("IntrinsicGeometryTests", text)
            self.assertIn("IntrinsicGeometryIoTests", text)

    def test_finalize_selects_owner_while_smoke_is_unadmitted(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            write_registry(build_dir)
            route = touched_scope.analyze_change_records(
                [record("src/geometry/Geometry.Mesh.cpp")]
            )
            finalized = touched_scope.finalize_route(route, build_dir)
            self.assertEqual(finalized["route"], "focused")
            self.assertEqual(
                finalized["finalization"]["selected_targets"],
                [
                    "GeometryContractTests",
                    "IntrinsicGeometryIoTests",
                    "IntrinsicGeometryTests",
                ],
            )
            self.assertFalse(finalized["smoke"]["focused_enabled"])
            self.assertEqual(
                finalized["smoke"]["policy"],
                "broad-only-pending-budget",
            )
            self.assertEqual(
                finalized["finalization"]["build_batches"],
                [
                    {
                        "name": "focused-owner",
                        "targets": [
                            "GeometryContractTests",
                            "IntrinsicGeometryIoTests",
                            "IntrinsicGeometryTests",
                        ],
                        "producer_targets": [
                            "GeometryContractTests",
                            "IntrinsicGeometryIoTests",
                            "IntrinsicGeometryTests",
                        ],
                    },
                ],
            )
            self.assertEqual(
                touched_scope._test_batches(finalized),
                [
                    {
                        "name": "focused-owner",
                        "selector": [
                            "-L",
                            "geometry",
                            "-LE",
                            "gpu|vulkan|slow|flaky-quarantine",
                        ],
                        "producer_targets": [
                            "GeometryContractTests",
                            "IntrinsicGeometryIoTests",
                            "IntrinsicGeometryTests",
                        ],
                    },
                ],
            )

    def test_focused_runtime_route_excludes_unadmitted_smoke(self) -> None:
        rows = [
            ("IntrinsicRuntimeContractTests", "contract,runtime"),
            ("IntrinsicRuntimeUnitTests", "runtime,unit"),
            ("IntrinsicRuntimeIntegrationTests", "integration,runtime"),
            (
                "IntrinsicRuntimeGraphicsCpuTests",
                "graphics,integration,runtime",
            ),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            write_registry(build_dir, rows)
            route = touched_scope.analyze_change_records(
                [record("src/runtime/Runtime.Engine.cpp")]
            )
            finalized = touched_scope.finalize_route(route, build_dir)
            self.assertEqual(
                finalized["finalization"]["selected_targets"],
                [
                    "IntrinsicRuntimeContractTests",
                    "IntrinsicRuntimeIntegrationTests",
                    "IntrinsicRuntimeUnitTests",
                ],
            )
            self.assertEqual(
                finalized["finalization"]["build_batches"],
                [
                    {
                        "name": "focused-owner",
                        "targets": [
                            "IntrinsicRuntimeContractTests",
                            "IntrinsicRuntimeIntegrationTests",
                            "IntrinsicRuntimeUnitTests",
                        ],
                        "producer_targets": [
                            "IntrinsicRuntimeContractTests",
                            "IntrinsicRuntimeIntegrationTests",
                            "IntrinsicRuntimeUnitTests",
                        ],
                    },
                ],
            )
            test_batches = touched_scope._test_batches(finalized)
            self.assertEqual(
                [batch["producer_targets"] for batch in test_batches],
                [
                    [
                        "IntrinsicRuntimeContractTests",
                        "IntrinsicRuntimeIntegrationTests",
                        "IntrinsicRuntimeUnitTests",
                    ],
                ],
            )

            direct_smoke_change = touched_scope.analyze_change_records(
                [record("tests/integration/runtime/Test.Runtime.cpp")]
            )
            widened = touched_scope.finalize_route(
                direct_smoke_change,
                build_dir,
            )
            self.assertEqual(widened["route"], "broad")
            self.assertEqual(
                [batch["name"] for batch in widened["finalization"]["build_batches"]],
                ["pr-fast", "pr-smoke"],
            )

    def test_missing_focused_anchor_widens_to_broad(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            rows = [
                ("RuntimeOwnerTests", "runtime,unit"),
                (
                    "IntrinsicRuntimeGraphicsCpuTests",
                    "graphics,integration,runtime",
                ),
            ]
            write_registry(build_dir, rows)
            route = touched_scope.analyze_change_records(
                [record("src/runtime/Runtime.Engine.cpp")]
            )
            finalized = touched_scope.finalize_route(route, build_dir)
            self.assertEqual(finalized["route"], "broad")
            self.assertEqual(
                finalized["finalization"]["build_batches"],
                [
                    {
                        "name": "pr-fast",
                        "targets": ["IntrinsicPrFastTests"],
                        "producer_targets": ["RuntimeOwnerTests"],
                    },
                    {
                        "name": "pr-smoke",
                        "targets": ["IntrinsicPrSmokeTests"],
                        "producer_targets": ["IntrinsicRuntimeGraphicsCpuTests"],
                    },
                ],
            )
            self.assertEqual(
                touched_scope._test_batches(finalized),
                [
                    {
                        "name": "pr-fast",
                        "selector": [
                            "-L",
                            "unit|contract",
                            "-LE",
                            "gpu|vulkan|slow|flaky-quarantine",
                        ],
                        "producer_targets": ["RuntimeOwnerTests"],
                    },
                    {
                        "name": "pr-smoke",
                        "selector": [
                            "-L",
                            "integration",
                            "-L",
                            "runtime",
                            "-L",
                            "graphics",
                            "-LE",
                            "gpu|vulkan|slow|flaky-quarantine",
                        ],
                        "producer_targets": ["IntrinsicRuntimeGraphicsCpuTests"],
                    },
                ],
            )

    def test_malformed_registry_and_aggregate_mismatch_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            write_registry(build_dir)
            registry = build_dir / "test-inventories" / "RegisteredTestTargets.tsv"
            registry.write_text("wrong\theader\n", encoding="utf-8")
            route = touched_scope.analyze_change_records(
                [record("src/geometry/Geometry.Mesh.cpp")]
            )
            with self.assertRaises(touched_scope.RouteError):
                touched_scope.finalize_route(route, build_dir)

        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            write_registry(build_dir, fast=["IntrinsicGeometryTests"])
            route = touched_scope.analyze_change_records(
                [record("src/geometry/Geometry.Mesh.cpp")]
            )
            with self.assertRaises(touched_scope.RouteError):
                touched_scope.finalize_route(route, build_dir)

        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            write_registry(
                build_dir,
                fast=[
                    "IntrinsicGeometryTests",
                    "GeometryContractTests",
                    "UndeclaredTestTarget",
                ],
            )
            route = touched_scope.analyze_change_records(
                [record("src/geometry/Geometry.Mesh.cpp")]
            )
            with self.assertRaisesRegex(
                touched_scope.RouteError,
                r"aggregate inventory references undeclared target\(s\): "
                r"UndeclaredTestTarget",
            ):
                touched_scope.finalize_route(route, build_dir)

    def test_duplicate_registry_target_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp)
            rows = [
                ("IntrinsicGeometryTests", "geometry,unit"),
                ("IntrinsicGeometryTests", "geometry,unit"),
                (
                    "IntrinsicRuntimeGraphicsCpuTests",
                    "graphics,integration,runtime",
                ),
            ]
            write_registry(
                build_dir,
                rows,
                fast=["IntrinsicGeometryTests"],
            )
            route = touched_scope.analyze_change_records(
                [record("src/geometry/Geometry.Mesh.cpp")]
            )
            with self.assertRaises(touched_scope.RouteError):
                touched_scope.finalize_route(route, build_dir)

    def test_build_action_records_unique_incremental_closure(self) -> None:
        route = {
            "schema": touched_scope.ROUTE_SCHEMA,
            "stage": "finalized",
            "route": "broad",
            "needs_cpp": True,
            "reasons": [],
            "structural_checks": [],
            "owner_labels": [],
            "anchor_targets": [],
            "actions": [],
            "finalization": {
                "build_batches": [
                    {"name": "fast", "targets": ["Fast"]},
                    {"name": "smoke", "targets": ["Smoke"]},
                ]
            },
        }
        completed = subprocess.CompletedProcess([], 0, "", "")
        with (
            mock.patch.object(
                touched_scope,
                "_ninja_commands",
                side_effect=[{"a", "b"}, {"b", "c"}],
            ),
            mock.patch.object(
                touched_scope,
                "_run_command",
                return_value=completed,
            ),
        ):
            code, result = touched_scope.execute_build(
                route,
                root=REPO_ROOT,
                build_dir=Path("build/fake"),
            )
        self.assertEqual(code, 0)
        self.assertEqual(result["build"]["ninja_edge_count"], 3)
        self.assertEqual(
            [
                batch["incremental_command_edge_count"]
                for batch in result["build"]["batches"]
            ],
            [2, 1],
        )

    def test_ctest_inventory_rejects_zero_and_duplicates(self) -> None:
        build_dir = Path("build/fake")
        fake_binary = str((build_dir / "bin" / "Fake").resolve())
        zero = subprocess.CompletedProcess(
            [],
            0,
            json.dumps({"tests": []}),
            "",
        )
        with mock.patch.object(touched_scope, "_run_command", return_value=zero):
            with self.assertRaises(touched_scope.RouteError):
                touched_scope._ctest_names(
                    build_dir,
                    ["-L", "unit"],
                    root=REPO_ROOT,
                    expected_targets=["Fake"],
                )

        duplicate = subprocess.CompletedProcess(
            [],
            0,
            json.dumps(
                {
                    "tests": [
                        {"name": "A", "command": [fake_binary]},
                        {"name": "A", "command": [fake_binary]},
                    ]
                }
            ),
            "",
        )
        with mock.patch.object(touched_scope, "_run_command", return_value=duplicate):
            with self.assertRaises(touched_scope.RouteError):
                touched_scope._ctest_names(
                    build_dir,
                    ["-L", "unit"],
                    root=REPO_ROOT,
                    expected_targets=["Fake"],
                )

        wrong_producer = subprocess.CompletedProcess(
            [],
            0,
            json.dumps(
                {
                    "tests": [
                        {
                            "name": "Other.Case",
                            "command": [str((build_dir / "bin" / "Other").resolve())],
                        }
                    ]
                }
            ),
            "",
        )
        with mock.patch.object(
            touched_scope,
            "_run_command",
            return_value=wrong_producer,
        ):
            with self.assertRaises(touched_scope.RouteError):
                touched_scope._ctest_names(
                    build_dir,
                    ["-L", "unit"],
                    root=REPO_ROOT,
                    expected_targets=["Fake"],
                )

    def test_exact_ctest_regexes_are_bounded_and_complete(self) -> None:
        names = [f"Fixture.LongCase_{index:04d}_{'x' * 80}" for index in range(80)]
        regexes = touched_scope._exact_name_regexes(
            names,
            maximum_length=1_000,
        )
        self.assertGreater(len(regexes), 1)
        self.assertTrue(all(len(regex) <= 1_000 for regex in regexes))
        for name in names:
            self.assertEqual(
                sum(re.escape(name) in regex for regex in regexes),
                1,
            )

    @unittest.skipUnless(
        all(
            subprocess.run(
                [tool, "--version"],
                check=False,
                capture_output=True,
            ).returncode
            == 0
            for tool in ("cmake", "ninja")
        ),
        "CMake and Ninja are required",
    )
    def test_pre_test_discovery_is_exact_only_after_binary_build(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            (source / "CMakeLists.txt").write_text(
                textwrap.dedent(
                    """\
                    cmake_minimum_required(VERSION 3.20)
                    project(PreTestFixture LANGUAGES CXX)
                    enable_testing()
                    include(GoogleTest)
                    add_executable(Fake fake.cpp)
                    set_target_properties(Fake PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
                    gtest_discover_tests(Fake
                      DISCOVERY_MODE PRE_TEST
                      PROPERTIES LABELS geometry)
                    """
                ),
                encoding="utf-8",
            )
            (source / "fake.cpp").write_text(
                textwrap.dedent(
                    r"""\
                    #include <iostream>
                    #include <string_view>
                    int main(int argc, char** argv) {
                      for (int i = 1; i < argc; ++i) {
                        if (std::string_view(argv[i]) == "--gtest_list_tests") {
                          std::cout << "Fixture.\n  ExactCase\n";
                          return 0;
                        }
                      }
                      return 0;
                    }
                    """
                ),
                encoding="utf-8",
            )
            subprocess.run(
                ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja"],
                check=True,
                capture_output=True,
            )
            before = subprocess.run(
                [
                    "ctest",
                    "--test-dir",
                    str(build),
                    "--show-only=json-v1",
                    "-L",
                    "geometry",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            before_names = [
                test["name"] for test in json.loads(before.stdout).get("tests", [])
            ]
            self.assertNotEqual(before_names, ["Fixture.ExactCase"])
            subprocess.run(
                ["cmake", "--build", str(build), "--target", "Fake"],
                check=True,
                capture_output=True,
            )
            names = touched_scope._ctest_names(
                build,
                ["-L", "geometry"],
                root=root,
                expected_targets=["Fake"],
            )
            self.assertEqual(names, ["Fixture.ExactCase"])
            route = {
                "stage": "built",
                "route": "focused",
                "owner_labels": ["geometry"],
                "finalization": {
                    "selected_targets": ["Fake"],
                    "build_batches": [
                        {
                            "name": "focused-owner",
                            "targets": ["Fake"],
                            "producer_targets": ["Fake"],
                        }
                    ],
                },
            }
            code, executed = touched_scope.execute_tests(
                route,
                root=root,
                build_dir=build,
                timeout=10,
                jobs=1,
            )
            self.assertEqual(code, 0)
            self.assertEqual(executed["test"]["selected_test_count"], 1)

    @unittest.skipUnless(
        all(
            subprocess.run(
                [tool, "--version"],
                check=False,
                capture_output=True,
            ).returncode
            == 0
            for tool in ("cmake", "ninja")
        ),
        "CMake and Ninja are required",
    )
    def test_broad_route_executes_fast_and_smoke_producers_separately(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            (source / "CMakeLists.txt").write_text(
                textwrap.dedent(
                    """\
                    cmake_minimum_required(VERSION 3.20)
                    project(BroadRouteFixture LANGUAGES CXX)
                    enable_testing()
                    include(GoogleTest)
                    add_executable(Owner owner.cpp)
                    add_executable(Smoke smoke.cpp)
                    set_target_properties(Owner Smoke PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
                    gtest_discover_tests(Owner
                      DISCOVERY_MODE PRE_TEST
                      PROPERTIES LABELS unit-geometry)
                    gtest_discover_tests(Smoke
                      DISCOVERY_MODE PRE_TEST
                      PROPERTIES LABELS integration-runtime-graphics)
                    """
                ),
                encoding="utf-8",
            )
            for target, suite in (("owner", "Owner"), ("smoke", "Smoke")):
                (source / f"{target}.cpp").write_text(
                    textwrap.dedent(
                        f"""\
                        #include <iostream>
                        #include <string_view>
                        int main(int argc, char** argv) {{
                          for (int i = 1; i < argc; ++i) {{
                            if (std::string_view(argv[i]) == "--gtest_list_tests") {{
                              std::cout << "{suite}.\\n  ExactCase\\n";
                              return 0;
                            }}
                          }}
                          return 0;
                        }}
                        """
                    ),
                    encoding="utf-8",
                )
            subprocess.run(
                ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja"],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["cmake", "--build", str(build), "--target", "Owner", "Smoke"],
                check=True,
                capture_output=True,
            )
            route = {
                "stage": "built",
                "route": "broad",
                "owner_labels": [],
                "finalization": {
                    "selected_targets": ["Owner", "Smoke"],
                    "build_batches": [
                        {
                            "name": "pr-fast",
                            "targets": ["Owner"],
                            "producer_targets": ["Owner"],
                        },
                        {
                            "name": "pr-smoke",
                            "targets": ["Smoke"],
                            "producer_targets": ["Smoke"],
                        },
                    ],
                },
            }
            code, executed = touched_scope.execute_tests(
                route,
                root=root,
                build_dir=build,
                timeout=10,
                jobs=1,
            )
            self.assertEqual(code, 0)
            self.assertEqual(
                [
                    (batch["name"], batch["selected_test_count"])
                    for batch in executed["test"]["batches"]
                ],
                [("pr-fast", 1), ("pr-smoke", 1)],
            )
            self.assertEqual(executed["test"]["selected_test_count"], 2)
            self.assertEqual(
                (build / "ci-routing" / "selected-ctest-tests.txt")
                .read_text(encoding="utf-8")
                .splitlines(),
                ["Owner.ExactCase", "Smoke.ExactCase"],
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
