#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
TEST_MODULE = REPO_ROOT / "cmake" / "IntrinsicTest.cmake"


def _configure_fixture(root: Path, body: str) -> subprocess.CompletedProcess[str]:
    source = root / "source"
    build = root / "build"
    source.mkdir()
    (source / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
    (source / "CMakeLists.txt").write_text(
        textwrap.dedent(
            f"""\
            cmake_minimum_required(VERSION 3.28)
            project(IntrinsicTestAggregateFixture LANGUAGES CXX)
            include("{TEST_MODULE}")
            {body}
            """
        ),
        encoding="utf-8",
    )
    return subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def _read_inventory(build: Path, name: str) -> set[str]:
    path = build / "test-inventories" / f"{name}.txt"
    return {
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    }


class TestBuildAggregateTests(unittest.TestCase):
    def test_aggregate_inventories_match_independent_label_selection(self) -> None:
        registrations = {
            "UnitCpu": {"unit", "core"},
            "ContractCpu": {"contract", "runtime"},
            "IntegrationCpu": {"integration", "runtime", "graphics"},
            "SlowCpu": {"integration", "runtime", "slow"},
            "GpuOnly": {"gpu", "graphics"},
            "VulkanOnly": {"vulkan", "graphics"},
            "GpuVulkan": {"gpu", "vulkan", "graphics"},
            "Quarantined": {"unit", "flaky-quarantine"},
            "BenchmarkCpu": {"benchmark", "geometry"},
        }
        registration_lines = []
        for target, labels in registrations.items():
            registration_lines.extend(
                (
                    f"add_executable({target} main.cpp)",
                    "intrinsic_register_test_executable("
                    f"TARGET {target} LABELS {' '.join(sorted(labels))})",
                )
            )
        body = "\n".join(
            [
                *registration_lines,
                "intrinsic_write_test_registry()",
                "intrinsic_add_test_aggregate(NAME IntrinsicTests)",
                (
                    "intrinsic_add_test_aggregate(NAME IntrinsicPrFastTests "
                    "INCLUDE_ANY unit contract "
                    "EXCLUDE_ANY gpu vulkan slow flaky-quarantine)"
                ),
                (
                    "intrinsic_add_test_aggregate(NAME IntrinsicCpuTests "
                    "EXCLUDE_ANY gpu vulkan slow flaky-quarantine)"
                ),
                (
                    "intrinsic_add_test_aggregate(NAME IntrinsicGpuVulkanTests "
                    "INCLUDE_ALL gpu vulkan "
                    "EXCLUDE_ANY slow flaky-quarantine)"
                ),
                (
                    "intrinsic_add_test_aggregate(NAME IntrinsicPrSmokeTests "
                    "INCLUDE_ALL integration runtime graphics "
                    "EXCLUDE_ANY gpu vulkan slow flaky-quarantine)"
                ),
            ]
        )

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            result = _configure_fixture(root, body)
            self.assertEqual(result.returncode, 0, msg=result.stdout)
            build = root / "build"

            excluded = {"gpu", "vulkan", "slow", "flaky-quarantine"}
            expected = {
                "IntrinsicTests": set(registrations),
                "IntrinsicPrFastTests": {
                    target
                    for target, labels in registrations.items()
                    if labels.intersection({"unit", "contract"})
                    and not labels.intersection(excluded)
                },
                "IntrinsicCpuTests": {
                    target
                    for target, labels in registrations.items()
                    if not labels.intersection(excluded)
                },
                "IntrinsicGpuVulkanTests": {
                    target
                    for target, labels in registrations.items()
                    if {"gpu", "vulkan"}.issubset(labels)
                    and not labels.intersection({"slow", "flaky-quarantine"})
                },
                "IntrinsicPrSmokeTests": {
                    target
                    for target, labels in registrations.items()
                    if {"integration", "runtime", "graphics"}.issubset(labels)
                    and not labels.intersection(excluded)
                },
            }

            for aggregate, members in expected.items():
                with self.subTest(aggregate=aggregate):
                    self.assertEqual(_read_inventory(build, aggregate), members)
                    query = subprocess.run(
                        ["ninja", "-C", str(build), "-t", "query", aggregate],
                        text=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        check=False,
                    )
                    self.assertEqual(query.returncode, 0, msg=query.stdout)
                    query_lines = query.stdout.splitlines()
                    input_index = query_lines.index("  input: phony")
                    output_index = query_lines.index("  outputs:")
                    dependencies = {
                        line.strip()
                        for line in query_lines[input_index + 1 : output_index]
                    }
                    for target in registrations:
                        present = any(
                            dependency == target
                            or dependency.endswith(f"/{target}")
                            for dependency in dependencies
                        )
                        if target in members:
                            self.assertTrue(present, target)
                        else:
                            self.assertFalse(present, target)

            registry_lines = (
                build
                / "test-inventories"
                / "RegisteredTestTargets.tsv"
            ).read_text(encoding="utf-8").splitlines()
            self.assertEqual(registry_lines[0], "target\tlabels")
            self.assertEqual(len(registry_lines), len(registrations) + 1)

    def test_invalid_registration_and_aggregate_metadata_fail_configure(self) -> None:
        cases = {
            "unknown label": """
                add_executable(TestTarget main.cpp)
                intrinsic_register_test_executable(
                    TARGET TestTarget LABELS unit undocumented)
            """,
            "duplicate label": """
                add_executable(TestTarget main.cpp)
                intrinsic_register_test_executable(
                    TARGET TestTarget LABELS unit unit)
            """,
            "duplicate registration": """
                add_executable(TestTarget main.cpp)
                intrinsic_register_test_executable(TARGET TestTarget LABELS unit)
                intrinsic_register_test_executable(TARGET TestTarget LABELS unit)
            """,
            "missing target": """
                intrinsic_register_test_executable(
                    TARGET MissingTarget LABELS unit)
            """,
            "non-executable target": """
                add_library(TestTarget STATIC main.cpp)
                intrinsic_register_test_executable(TARGET TestTarget LABELS unit)
            """,
            "unknown aggregate label": """
                add_executable(TestTarget main.cpp)
                intrinsic_register_test_executable(TARGET TestTarget LABELS unit)
                intrinsic_add_test_aggregate(
                    NAME InvalidAggregate INCLUDE_ANY undocumented)
            """,
            "conflicting aggregate target": """
                add_executable(TestTarget main.cpp)
                intrinsic_register_test_executable(TARGET TestTarget LABELS unit)
                add_custom_target(IntrinsicTests)
                intrinsic_add_test_aggregate(NAME IntrinsicTests)
            """,
        }

        for name, body in cases.items():
            with self.subTest(case=name), tempfile.TemporaryDirectory() as tmp:
                result = _configure_fixture(Path(tmp), body)
                self.assertNotEqual(result.returncode, 0, msg=result.stdout)
                self.assertIn("CMake Error", result.stdout)


if __name__ == "__main__":
    unittest.main()
