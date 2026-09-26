#!/usr/bin/env python3
"""Synthetic cases for tools/repo/check_compiler_hazards.py (BUG-223)."""

import importlib.util
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("hazards", ROOT / "tools/repo/check_compiler_hazards.py")
hazards = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hazards)


def expect(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}")
        sys.exit(1)


flagged = [
    "Result Extract(const Mesh& mesh, const Params& params = kScalarDefaults);",
    "void F(const ns::Options& o = ns::kDefaults, int x);",
    "void G(\n    const std::array<int, 3>& a =\n        kTriple);",
]
allowed = [
    "Result Extract(const Mesh& mesh, const Params& params = {});",
    "void F(const Options& o = Options{});",
    "void G(const Foo* p = nullptr);",
    "const auto& value = kDefaults;",
    "void H(Params params = kDefaults);",
    "// void I(const Params& p = kDefaults);",
    "for (const auto& x : items) {}",
]
for text in flagged:
    expect(len(hazards.scan_text(text)) == 1, f"not flagged: {text!r}")
for text in allowed:
    expect(not hazards.scan_text(text), f"false positive: {text!r} -> {hazards.scan_text(text)}")

with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp) / "a.cppm").write_text(flagged[0])
    expect(hazards.main(["--root", tmp]) == 1, "main must fail on a finding")
    (Path(tmp) / "a.cppm").write_text(allowed[0])
    expect(hazards.main(["--root", tmp]) == 0, "main must pass a clean tree")
expect(hazards.main(["--root", str(ROOT / "src"), "--root", str(ROOT / "methods")]) == 0,
       "the repository must be free of compiler hazards")
print("check_compiler_hazards: all cases passed")
