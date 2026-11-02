#!/usr/bin/env python3
"""ARCH-022: the plugin build id covers the build settings and every module interface (cmake/PluginBuildId.cmake)."""
from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "cmake" / "PluginBuildId.cmake"
CMAKE = shutil.which("cmake") or "cmake"


class PluginBuildIdScript(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp())
        self.root = self.tmp / "src"
        self.out = self.tmp / "PluginBuildId.h"
        # A StrongHandle-like layout: the runtime interface a plugin sees names a type defined two levels away.
        self.write("core/Core.StrongHandle.cppm", "export module Core.StrongHandle; export struct Handle { int Index; };\n")
        self.write("runtime/Kernel/Runtime.WorldHandle.cppm", "export module Runtime.WorldHandle; import Core.StrongHandle;\n")
        self.write("runtime/Kernel/Runtime.JobService.cpp", "module Runtime.JobService;\n")

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp)

    def write(self, relative: str, text: str) -> None:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def build_id(self, settings: str = "clang 24|Debug|address,undefined|shared") -> str:
        subprocess.run([CMAKE, f"-DROOT={self.root}", f"-DOUT={self.out}", f"-DSETTINGS={settings}", "-DDEFINITIONS=",
                        "-P", str(SCRIPT)],
                       check=True, capture_output=True, text=True)
        match = re.fullmatch(r'#define INTRINSIC_PLUGIN_BUILD_ID "([0-9a-f]{64})"\n', self.out.read_text(encoding="utf-8"))
        self.assertIsNotNone(match, self.out.read_text(encoding="utf-8"))
        return match.group(1)

    def test_a_nested_interface_edit_changes_the_id(self) -> None:
        before = self.build_id()
        self.write("core/Core.StrongHandle.cppm", "export module Core.StrongHandle; export struct Handle { long Index; };\n")
        self.assertNotEqual(self.build_id(), before)

    def test_a_new_interface_changes_the_id(self) -> None:
        before = self.build_id()
        self.write("runtime/Services/Runtime.New.cppm", "export module Runtime.New;\n")
        self.assertNotEqual(self.build_id(), before)

    def test_an_implementation_edit_keeps_the_id(self) -> None:
        before = self.build_id()
        self.write("runtime/Kernel/Runtime.JobService.cpp", "module Runtime.JobService; // edited\n")
        self.assertEqual(self.build_id(), before)

    def test_an_unchanged_id_leaves_the_header_untouched(self) -> None:
        # Dependents (the host's loader and every plugin) rebuild only when the id changes.
        self.build_id()
        written = self.out.stat().st_mtime_ns
        self.build_id()
        self.assertEqual(self.out.stat().st_mtime_ns, written)


class PluginBuildIdWiring(unittest.TestCase):
    """intrinsic_add_plugin_build_id in a small CMake project built with Ninja, as the runtime uses it."""

    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp())
        self.project, self.build = self.tmp / "project", self.tmp / "build"
        (self.project / "src/core").mkdir(parents=True)
        (self.project / "src/core/Core.Handle.cppm").write_text("export module Core.Handle;\n", encoding="utf-8")
        (self.project / "src/core/Core.Unused.cppm").write_text("export module Core.Unused;\n", encoding="utf-8")
        (self.project / "host.cpp").write_text('#include "PluginBuildId.h"\nconst char* kId = INTRINSIC_PLUGIN_BUILD_ID;\n',
                                               encoding="utf-8")
        (self.project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.28)\nproject(PluginBuildIdWiring LANGUAGES CXX)\n"
            f'include("{SCRIPT.as_posix()}")\n'
            "add_library(host STATIC host.cpp)\n"
            "if(HOST_DEFINITION)\n  target_compile_definitions(host PRIVATE ${HOST_DEFINITION})\nendif()\n"
            "intrinsic_add_plugin_build_id(host ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)\n",
            encoding="utf-8")

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp)

    def build_id(self, *configure: str) -> str:
        if configure or not (self.build / "build.ninja").exists():
            subprocess.run([CMAKE, "-S", str(self.project), "-B", str(self.build), "-G", "Ninja",
                            "-DCMAKE_BUILD_TYPE=Debug", *configure], check=True, capture_output=True, text=True)
        subprocess.run([CMAKE, "--build", str(self.build)], check=True, capture_output=True, text=True)
        text = (self.build / "generated/PluginBuildId.h").read_text(encoding="utf-8")
        return re.fullmatch(r'#define INTRINSIC_PLUGIN_BUILD_ID "([0-9a-f]{64})"\n', text).group(1)

    def test_removing_an_interface_changes_the_id_in_an_incremental_build(self) -> None:
        before = self.build_id()
        (self.project / "src/core/Core.Unused.cppm").unlink()
        self.assertNotEqual(self.build_id(), before)

    def test_configuration_specific_flags_change_the_id(self) -> None:
        before = self.build_id("-DCMAKE_CXX_FLAGS_DEBUG=-g")
        self.assertNotEqual(self.build_id("-DCMAKE_CXX_FLAGS_DEBUG=-g -D_GLIBCXX_DEBUG"), before)

    def test_the_targets_compile_definitions_change_the_id(self) -> None:
        before = self.build_id("-DHOST_DEFINITION=")
        self.assertNotEqual(self.build_id("-DHOST_DEFINITION=_GLIBCXX_DEBUG"), before)


if __name__ == "__main__":
    unittest.main()
