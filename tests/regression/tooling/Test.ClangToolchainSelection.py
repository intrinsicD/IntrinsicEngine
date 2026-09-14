#!/usr/bin/env python3
"""Keep auto-selected driver paths stable through directory and executable aliases."""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]


class ClangToolchainSelectionTests(unittest.TestCase):
    def test_auto_selection_returns_real_directories(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-clang-select-") as temporary:
            root = Path(temporary)
            install = root / "real install"
            directory = install / "bin"
            directory.mkdir(parents=True)
            for name in ("clang-99", "clang-scan-deps-99"):
                driver = directory / name
                driver.write_text("#!/bin/sh\necho 'clang version 99.0.0'\n")
                driver.chmod(0o755)
            (directory / "clang++-99").symlink_to("clang-99")
            alias = root / "alias"
            alias.symlink_to(install, target_is_directory=True)
            script = root / "check.cmake"
            result = root / "result.txt"
            script.write_text(
                f'include("{REPO_ROOT}/cmake/ClangToolchainSelection.cmake")\n'
                f'set(ENV{{LLVM_DIR}} "{alias}")\n'
                'intrinsic_find_highest_complete_clang_toolchain(c cxx scan major)\n'
                f'file(WRITE "{result}" "${{c}}\\n${{cxx}}\\n${{scan}}\\n${{major}}\\n")\n'
            )
            subprocess.run(["cmake", "-P", str(script)], check=True, capture_output=True)
            self.assertEqual(result.read_text().splitlines(), [
                str(directory / "clang-99"), str(directory / "clang++-99"),
                str(directory / "clang-scan-deps-99"), "99",
            ])

    def test_directory_alias_is_resolved_without_changing_cxx_driver_name(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-clang-path-") as temporary:
            root = Path(temporary)
            directory = root / "real install" / "bin"
            directory.mkdir(parents=True)
            (directory / "clang-23").write_text("driver fixture\n")
            (directory / "clang++-23").symlink_to("clang-23")
            alias = root / "alias"
            alias.symlink_to(directory, target_is_directory=True)
            script = root / "check.cmake"
            result = root / "result.txt"
            script.write_text(
                f'include("{REPO_ROOT}/cmake/ClangToolchainSelection.cmake")\n'
                f'intrinsic_clang_driver_path(cxx "{alias}/clang++-23")\n'
                f'intrinsic_clang_driver_path(c "{alias}/clang-23")\n'
                f'intrinsic_clang_driver_path(direct "{directory}/clang++-23")\n'
                'intrinsic_clang_driver_path(missing "")\n'
                f'file(WRITE "{result}" "${{cxx}}\\n${{c}}\\n${{direct}}\\n[${{missing}}]\\n")\n'
            )
            subprocess.run(["cmake", "-P", str(script)], check=True, capture_output=True)
            self.assertEqual(result.read_text().splitlines(), [
                str(directory / "clang++-23"), str(directory / "clang-23"),
                str(directory / "clang++-23"), "[]",
            ])


if __name__ == "__main__":
    unittest.main()
