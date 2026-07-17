#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import subprocess
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SHARED_VCPKG_INSTALLED_DIR = REPO_ROOT / "external" / "vcpkg-installed" / "ci"
CACHE_IDENTITY_KEYS = (
    "EXTRINSIC_PLATFORM",
    "EXTRINSIC_BACKEND",
    "INTRINSIC_PLATFORM_BACKEND",
    "INTRINSIC_PLATFORM_BACKEND_SELECTED",
    "INTRINSIC_HEADLESS_NO_GLFW",
)
PATH_HASHED_SHADER_TARGET = re.compile(r"IntrinsicShaders_[0-9a-f]{16}")


@dataclass(frozen=True)
class Configuration:
    name: str
    preset: str
    values: tuple[tuple[str, str], ...]
    expected_identity: tuple[tuple[str, str], ...]


@dataclass(frozen=True)
class Inventory:
    targets: tuple[str, ...]
    registered_test_targets: tuple[str, ...]
    ctest_tests: tuple[str, ...]
    identity: tuple[tuple[str, str], ...]


NULL_HEADLESS = Configuration(
    name="null-headless",
    preset="ci",
    values=(
        ("EXTRINSIC_PLATFORM", "Linux"),
        ("EXTRINSIC_BACKEND", "Null"),
        ("INTRINSIC_PLATFORM_BACKEND", "Null"),
        ("INTRINSIC_HEADLESS_NO_GLFW", "ON"),
    ),
    expected_identity=(
        ("EXTRINSIC_PLATFORM", "Linux"),
        ("EXTRINSIC_BACKEND", "Null"),
        ("INTRINSIC_PLATFORM_BACKEND", "Null"),
        ("INTRINSIC_PLATFORM_BACKEND_SELECTED", "Null"),
        ("INTRINSIC_HEADLESS_NO_GLFW", "ON"),
    ),
)

VULKAN_GLFW = Configuration(
    name="vulkan-glfw",
    preset="ci",
    values=(
        ("EXTRINSIC_PLATFORM", "Linux"),
        ("EXTRINSIC_BACKEND", "Vulkan"),
        ("INTRINSIC_PLATFORM_BACKEND", "Glfw"),
        ("INTRINSIC_HEADLESS_NO_GLFW", "OFF"),
    ),
    expected_identity=(
        ("EXTRINSIC_PLATFORM", "Linux"),
        ("EXTRINSIC_BACKEND", "Vulkan"),
        ("INTRINSIC_PLATFORM_BACKEND", "Glfw"),
        ("INTRINSIC_PLATFORM_BACKEND_SELECTED", "Glfw"),
        ("INTRINSIC_HEADLESS_NO_GLFW", "OFF"),
    ),
)

AUTO_VULKAN = Configuration(
    name="auto-vulkan",
    preset="dev",
    values=(
        ("INTRINSIC_BUILD_SANDBOX", "OFF"),
        ("INTRINSIC_PLATFORM_BACKEND", "Auto"),
        ("INTRINSIC_HEADLESS_NO_GLFW", "OFF"),
    ),
    expected_identity=(
        ("EXTRINSIC_PLATFORM", "Linux"),
        ("EXTRINSIC_BACKEND", "Vulkan"),
        ("INTRINSIC_PLATFORM_BACKEND", "Auto"),
        ("INTRINSIC_PLATFORM_BACKEND_SELECTED", "Glfw"),
        ("INTRINSIC_HEADLESS_NO_GLFW", "OFF"),
    ),
)


def _run(command: list[str], context: str) -> str:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=300,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"{context} failed with exit code {result.returncode}:\n"
            f"{result.stdout[-16000:]}"
        )
    return result.stdout


def _configure(
    build_dir: Path,
    configuration: Configuration,
    *,
    fresh: bool,
) -> None:
    command = [
        "cmake",
        "--preset",
        configuration.preset,
        "-B",
        str(build_dir),
        f"-DVCPKG_INSTALLED_DIR:PATH={SHARED_VCPKG_INSTALLED_DIR}",
    ]
    if fresh:
        command.append("--fresh")
    command.extend(f"-D{key}={value}" for key, value in configuration.values)
    _run(command, f"{configuration.name} configure in {build_dir}")


def _read_cache(build_dir: Path) -> dict[str, str]:
    cache: dict[str, str] = {}
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        declaration, value = line.split("=", 1)
        key = declaration.split(":", 1)[0]
        cache[key] = value
    return cache


def _normalize_path(value: str, build_dir: Path) -> str:
    normalized = value.replace(str(build_dir), "<BUILD>").replace(
        str(REPO_ROOT), "<SOURCE>"
    )
    return PATH_HASHED_SHADER_TARGET.sub(
        "IntrinsicShaders_<OUTPUT_PATH_HASH>", normalized
    )


def _target_inventory(build_dir: Path, cache: dict[str, str]) -> tuple[str, ...]:
    ninja = cache.get("CMAKE_MAKE_PROGRAM")
    if not ninja:
        raise AssertionError("CMAKE_MAKE_PROGRAM is missing from CMakeCache.txt")
    output = _run(
        [ninja, "-C", str(build_dir), "-t", "targets"],
        f"Ninja target inventory for {build_dir}",
    )
    return tuple(
        sorted(
            _normalize_path(line.strip(), build_dir)
            for line in output.splitlines()
            if line.strip()
        )
    )


def _registered_test_target_inventory(build_dir: Path) -> tuple[str, ...]:
    inventory_path = build_dir / "test-inventories" / "RegisteredTestTargets.tsv"
    lines = inventory_path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "target\tlabels":
        raise AssertionError(
            f"invalid registered test target inventory: {inventory_path}"
        )

    normalized: list[str] = []
    for line in lines[1:]:
        if not line:
            continue
        try:
            target, labels = line.split("\t", 1)
        except ValueError as error:
            raise AssertionError(
                f"invalid registered test target row: {line!r}"
            ) from error
        normalized_labels = ",".join(
            sorted(label for label in labels.split(",") if label)
        )
        normalized.append(f"{target}\t{normalized_labels}")
    return tuple(sorted(normalized))


def _ctest_inventory(build_dir: Path) -> tuple[str, ...]:
    output = _run(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
        f"CTest inventory for {build_dir}",
    )
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"CTest emitted invalid JSON for {build_dir}:\n{output[-16000:]}"
        ) from error
    return tuple(sorted(test["name"] for test in payload["tests"]))


def _snapshot(build_dir: Path) -> Inventory:
    cache = _read_cache(build_dir)
    missing = [key for key in CACHE_IDENTITY_KEYS if key not in cache]
    if missing:
        raise AssertionError(
            f"configure identity missing from CMakeCache.txt: {missing}"
        )
    return Inventory(
        targets=_target_inventory(build_dir, cache),
        registered_test_targets=_registered_test_target_inventory(build_dir),
        ctest_tests=_ctest_inventory(build_dir),
        identity=tuple((key, cache[key]) for key in CACHE_IDENTITY_KEYS),
    )


class BackendConfigureDeterminismTests(unittest.TestCase):
    def assert_inventory_equal(
        self,
        expected: Inventory,
        actual: Inventory,
        *,
        context: str,
    ) -> None:
        for component in (
            "targets",
            "registered_test_targets",
            "ctest_tests",
            "identity",
        ):
            with self.subTest(context=context, component=component):
                self.assertEqual(
                    getattr(expected, component),
                    getattr(actual, component),
                )

    def test_backend_configure_graph_is_history_independent(self) -> None:
        stable: dict[str, Inventory] = {}

        with tempfile.TemporaryDirectory(
            prefix="intrinsic-backend-configure-"
        ) as temporary_root:
            root = Path(temporary_root)
            for configuration in (NULL_HEADLESS, VULKAN_GLFW, AUTO_VULKAN):
                first_tree = root / f"{configuration.name}-first"
                second_tree = root / f"{configuration.name}-second"

                _configure(first_tree, configuration, fresh=True)
                first_fresh = _snapshot(first_tree)
                self.assertEqual(
                    first_fresh.identity,
                    configuration.expected_identity,
                    msg=f"unexpected {configuration.name} configure identity",
                )

                _configure(first_tree, configuration, fresh=False)
                same_tree_reconfigure = _snapshot(first_tree)
                self.assert_inventory_equal(
                    first_fresh,
                    same_tree_reconfigure,
                    context=f"{configuration.name}: fresh vs reconfigure",
                )

                _configure(second_tree, configuration, fresh=True)
                second_fresh = _snapshot(second_tree)
                self.assert_inventory_equal(
                    first_fresh,
                    second_fresh,
                    context=f"{configuration.name}: first vs second fresh",
                )
                stable[configuration.name] = first_fresh

            explicit_vulkan = stable[VULKAN_GLFW.name]
            auto_vulkan = stable[AUTO_VULKAN.name]
            for component in (
                "targets",
                "registered_test_targets",
                "ctest_tests",
            ):
                with self.subTest(auto_resolution_component=component):
                    self.assertEqual(
                        getattr(explicit_vulkan, component),
                        getattr(auto_vulkan, component),
                    )

            changed_tree = root / f"{NULL_HEADLESS.name}-first"
            null_inventory = stable[NULL_HEADLESS.name]
            _configure(changed_tree, VULKAN_GLFW, fresh=False)
            changed_inventory = _snapshot(changed_tree)

            self.assertNotEqual(null_inventory.identity, changed_inventory.identity)
            changed_components = [
                component
                for component in (
                    "targets",
                    "registered_test_targets",
                    "ctest_tests",
                )
                if getattr(null_inventory, component)
                != getattr(changed_inventory, component)
            ]
            self.assertTrue(
                changed_components,
                msg="Null/headless -> Vulkan/Glfw did not change the graph",
            )
            self.assertTrue(
                any(
                    "ExtrinsicBackendsVulkan" in target
                    for target in changed_inventory.targets
                ),
                msg="Vulkan target family is absent after explicit identity change",
            )
            self.assert_inventory_equal(
                explicit_vulkan,
                changed_inventory,
                context="changed Null tree vs fresh Vulkan/Glfw tree",
            )


if __name__ == "__main__":
    unittest.main()
