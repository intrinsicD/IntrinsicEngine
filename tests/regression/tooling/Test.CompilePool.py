#!/usr/bin/env python3
"""Compiles share a Ninja pool sized from the host's RAM (cmake/CompilePool.cmake): it limits how many compiles of one
build tree run at once (not links, not other trees)."""
from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "cmake" / "CompilePool.cmake"
CMAKE = shutil.which("cmake") or "cmake"


def depth(total_mib: int) -> int:
    result = subprocess.run([CMAKE, f"-DTOTAL_MIB={total_mib}", "-P", str(SCRIPT)],
                            check=True, capture_output=True, text=True)
    return int(result.stderr.strip())


class CompilePool(unittest.TestCase):
    def test_one_compile_per_4096_mib_and_never_none(self) -> None:
        self.assertEqual(depth(63488), 15)  # this workstation: 62 GiB
        self.assertEqual(depth(16384), 4)   # a 16 GiB CI runner
        self.assertEqual(depth(2048), 1)

    @unittest.skipUnless(shutil.which("ninja"), "ninja unavailable")
    def test_a_configured_tree_runs_its_compiles_in_the_pool(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source, build = Path(tmp) / "src", Path(tmp) / "build"
            source.mkdir()
            (source / "a.c").write_text("int a(void) { return 0; }\n")
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(pool LANGUAGES C)\n"
                f'include("{SCRIPT.as_posix()}")\nadd_library(a STATIC a.c)\n')
            subprocess.run([CMAKE, "-G", "Ninja", "-S", str(source), "-B", str(build)],
                           check=True, capture_output=True, text=True)
            ninja = (build / "build.ninja").read_text() + "".join(
                p.read_text() for p in build.glob("CMakeFiles/rules.ninja"))
            self.assertRegex(ninja, r"pool intrinsic_compile\n\s+depth = [1-9]")
            compile_edge = next(block for block in ninja.split("\n\n") if "a.c.o:" in block)
            self.assertIn("pool = intrinsic_compile", compile_edge)


if __name__ == "__main__":
    unittest.main()
