#!/usr/bin/env python3
"""ARCH-022 S3: configures, builds and runs tests/unit/core/tsan (the triple buffer and the append log under
ThreadSanitizer) apart from the calling tree. A script test (tools/ci/cpu_test_selection.py SCRIPT_TESTS); CTest
disables it when the compiler has no TSan runtime (INTRINSIC_TSAN_AVAILABLE).
Usage: Test.TsanHarness.py --build <dir> --compiler <c++> --scan-deps <clang-scan-deps> --ninja <ninja>
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    parser = argparse.ArgumentParser()
    for name in ("--build", "--compiler", "--scan-deps", "--ninja"):
        parser.add_argument(name, required=True)
    args = parser.parse_args()
    build = Path(args.build)
    steps = [["cmake", "-S", str(REPO_ROOT / "tests/unit/core/tsan"), "-B", str(build), "-G", "Ninja",
              f"-DCMAKE_MAKE_PROGRAM={args.ninja}", f"-DCMAKE_CXX_COMPILER={args.compiler}",
              f"-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS={args.scan_deps}", f"-DINTRINSIC_SOURCE_DIR={REPO_ROOT}",
              "-DCMAKE_BUILD_TYPE=Debug"],
             ["cmake", "--build", str(build)],
             [str(build / "tsan_harness")]]
    env = dict(os.environ, TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1")
    for step in steps:
        result = subprocess.run(step, capture_output=True, text=True, env=env, timeout=500)
        print(result.stdout[-4000:], result.stderr[-4000:], sep="\n")
        if result.returncode != 0:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
