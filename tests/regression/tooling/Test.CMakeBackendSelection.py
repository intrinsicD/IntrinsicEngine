#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
BACKEND_OPTIONS = REPO_ROOT / "cmake" / "IntrinsicBackendOptions.cmake"
ROOT_CMAKE = REPO_ROOT / "CMakeLists.txt"
RENDERER_BACKENDS = REPO_ROOT / "src" / "graphics" / "renderer" / "Backends" / "CMakeLists.txt"
PLATFORM_CMAKE = REPO_ROOT / "src" / "platform" / "CMakeLists.txt"


def _fixture_text(*, late_defaults: bool) -> str:
    return textwrap.dedent(
        f"""\
        cmake_minimum_required(VERSION 3.28)
        project(IntrinsicBackendSelectionFixture LANGUAGES NONE)

        option(INTRINSIC_HEADLESS_NO_GLFW "Headless fixture" OFF)

        # Exercise the production renderer conditional without descending into
        # either concrete backend. The Vulkan branch creates only a target marker.
        function(add_subdirectory source_dir)
            if("${{source_dir}}" MATCHES "/src/graphics/vulkan$")
                add_custom_target(ExtrinsicBackendsVulkan)
            endif()
        endfunction()

        # Platform selection is evaluated from the production CMake file; its
        # target-population commands are irrelevant to this configure-only probe.
        function(intrinsic_add_module_library)
        endfunction()
        function(target_sources)
        endfunction()
        function(target_include_directories)
        endfunction()
        function(target_link_libraries)
        endfunction()
        function(target_compile_definitions)
        endfunction()

        set(_probe_late_defaults {"ON" if late_defaults else "OFF"})
        if(NOT _probe_late_defaults)
            include("{BACKEND_OPTIONS.as_posix()}")
        endif()

        include("{RENDERER_BACKENDS.as_posix()}")

        # Negative-control mode reproduces the old topology: renderer observes
        # an empty value, then platform-time defaulting persists Vulkan in cache.
        if(_probe_late_defaults)
            if(NOT DEFINED EXTRINSIC_PLATFORM)
                set(EXTRINSIC_PLATFORM "${{CMAKE_SYSTEM_NAME}}" CACHE STRING
                    "Host platform selection")
            endif()
            if(NOT DEFINED EXTRINSIC_BACKEND)
                set(EXTRINSIC_BACKEND "Vulkan" CACHE STRING
                    "Graphics backend selection")
            endif()
        endif()

        include("{PLATFORM_CMAKE.as_posix()}")

        if(TARGET ExtrinsicBackendsVulkan)
            set(_probe_vulkan_target ON)
        else()
            set(_probe_vulkan_target OFF)
        endif()
        get_property(_probe_backend_choices CACHE EXTRINSIC_BACKEND PROPERTY STRINGS)
        file(WRITE "${{CMAKE_BINARY_DIR}}/backend-selection.txt"
            "graphics=${{EXTRINSIC_BACKEND}}\n"
            "platform=${{INTRINSIC_PLATFORM_BACKEND_SELECTED}}\n"
            "vulkan_target=${{_probe_vulkan_target}}\n"
            "backend_choices=${{_probe_backend_choices}}\n")
        """
    )


def _snapshot(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            raise AssertionError(f"invalid backend-selection row: {line!r}")
        result[key] = value
    return result


class CMakeBackendSelectionTests(unittest.TestCase):
    def _configure_clean_and_warm(
        self,
        *,
        backend: str | None = None,
        headless: bool = False,
        late_defaults: bool = False,
    ) -> tuple[dict[str, str], dict[str, str]]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            (source / "CMakeLists.txt").write_text(
                _fixture_text(late_defaults=late_defaults), encoding="utf-8"
            )

            command = [
                "cmake",
                "-S",
                str(source),
                "-B",
                str(build),
                "-G",
                "Ninja",
                "-DEXTRINSIC_PLATFORM=Linux",
                "-DINTRINSIC_PLATFORM_BACKEND=Auto",
                f"-DINTRINSIC_HEADLESS_NO_GLFW={'ON' if headless else 'OFF'}",
            ]
            if backend is not None:
                command.append(f"-DEXTRINSIC_BACKEND={backend}")

            snapshots: list[dict[str, str]] = []
            for phase in ("clean", "warm"):
                configured = subprocess.run(
                    command,
                    cwd=REPO_ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
                self.assertEqual(
                    configured.returncode,
                    0,
                    msg=f"{phase} configure failed:\n{configured.stdout}",
                )
                snapshots.append(_snapshot(build / "backend-selection.txt"))

            return snapshots[0], snapshots[1]

    def test_production_defaults_are_owned_before_renderer_configuration(self) -> None:
        root_text = ROOT_CMAKE.read_text(encoding="utf-8")
        platform_text = PLATFORM_CMAKE.read_text(encoding="utf-8")
        options_text = BACKEND_OPTIONS.read_text(encoding="utf-8")

        include_index = root_text.index("include(cmake/IntrinsicBackendOptions.cmake)")
        renderer_index = root_text.index(
            "add_subdirectory(${INTRINSIC_GRAPHICS_RENDERER_SOURCE_ROOT})"
        )
        self.assertLess(include_index, renderer_index)
        self.assertNotIn("set(EXTRINSIC_PLATFORM", platform_text)
        self.assertNotIn("set(EXTRINSIC_BACKEND", platform_text)
        self.assertIn("set(EXTRINSIC_PLATFORM", options_text)
        self.assertIn("set(EXTRINSIC_BACKEND", options_text)
        self.assertIn(
            "set_property(CACHE EXTRINSIC_BACKEND PROPERTY STRINGS Vulkan Null)",
            options_text,
        )

    def test_default_and_explicit_backend_graphs_are_clean_warm_stable(self) -> None:
        cases = (
            ("default Vulkan", None, False, "Vulkan", "Glfw", "ON"),
            ("explicit Vulkan", "Vulkan", False, "Vulkan", "Glfw", "ON"),
            ("headless Vulkan", "Vulkan", True, "Vulkan", "Null", "OFF"),
            ("explicit Null", "Null", False, "Null", "Null", "OFF"),
        )
        for name, backend, headless, graphics, platform, target in cases:
            with self.subTest(case=name):
                clean, warm = self._configure_clean_and_warm(
                    backend=backend, headless=headless
                )
                self.assertEqual(clean, warm)
                self.assertEqual(clean["graphics"], graphics)
                self.assertEqual(clean["platform"], platform)
                self.assertEqual(clean["vulkan_target"], target)
                self.assertEqual(clean["backend_choices"], "Vulkan;Null")

    def test_negative_control_detects_the_pre_fix_late_default(self) -> None:
        clean, warm = self._configure_clean_and_warm(late_defaults=True)
        self.assertEqual(clean["graphics"], "Vulkan")
        self.assertEqual(warm["graphics"], "Vulkan")
        self.assertEqual(clean["platform"], "Glfw")
        self.assertEqual(warm["platform"], "Glfw")
        self.assertEqual(clean["vulkan_target"], "OFF")
        self.assertEqual(warm["vulkan_target"], "ON")
        self.assertNotEqual(clean, warm)


if __name__ == "__main__":
    unittest.main()
