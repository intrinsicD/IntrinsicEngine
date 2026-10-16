#!/usr/bin/env python3
"""Synthetic cases for tools/repo/check_source_rules.py and its mutation self-check."""

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/repo"))
import check_source_rules as checker  # noqa: E402

Rule = checker.Rule


def expect(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}")
        sys.exit(1)


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "src/a").mkdir(parents=True)
    (root / "src/a/Unit.cpp").write_text(
        "void Before() { Beta(); }\nvoid Run()\n{\n    Alpha();\n    Beta();\n    Beta();\n}\n"
        "void After() { Gamma(); }\n")
    (root / "src/a/Other.cppm").write_text("export module Unit;\n")
    (root / "src/a/Notes.md").write_text("Delta\n")

    def passes(rule: Rule) -> bool:
        return not checker.check_rule(rule, root)

    unit = ("src/a/Unit.cpp",)
    run = ("void Run()", "void After()")
    # Each rule kind passes on matching input and fails on its violation.
    expect(passes(Rule("S", "require", files=unit, require=("Alpha();",))), "require passes")
    expect(not passes(Rule("S", "require-miss", files=unit, require=("Omega();",))), "require fails")
    expect(passes(Rule("S", "forbid", files=unit, slice=run, forbid=("Gamma",))), "slice hides Gamma")
    expect(not passes(Rule("S", "forbid-hit", files=unit, forbid=("Gamma",))), "forbid fails")
    expect(passes(Rule("S", "order", files=unit, order=("Alpha", "Beta", "Gamma"))), "order passes")
    expect(not passes(Rule("S", "order-bad", files=unit, order=("Gamma", "Alpha"))), "order fails")
    expect(passes(Rule("S", "first", files=unit, first_order=("Before", "Alpha", "Gamma"))), "first_order passes")
    # Chained order accepts `Beta` after `Alpha`, but its first occurrence precedes `Alpha`.
    expect(passes(Rule("S", "chained", files=unit, order=("Alpha", "Beta"))), "chained order passes")
    expect(not passes(Rule("S", "first-bad", files=unit, first_order=("Alpha", "Beta"))), "first_order fails")
    expect(not passes(Rule("S", "first-missing", files=unit, first_order=("Alpha", "Omega"))),
           "missing first_order token fails")
    expect(passes(Rule("S", "count", files=unit, count=(("Beta();", 3),))), "count passes")
    expect(not passes(Rule("S", "count-bad", files=unit, count=(("Beta();", 2),))), "count fails")
    expect(not passes(Rule("S", "prefix", files=unit, slice=("", "void Run()"), require=("Alpha",))),
           "empty begin marker slices from the file start")
    expect(passes(Rule("S", "compact", files=unit, compact=True, require=("{Alpha();Beta();",))), "compact passes")
    expect(passes(Rule("S", "absent", absent=("src/a/Retired.cpp",))), "absent passes")
    expect(not passes(Rule("S", "absent-hit", absent=("src/a/Other.cppm",))), "absent fails")
    expect(passes(Rule("S", "tree", trees=("src",), forbid=("Delta",))), "tree skips non-source files")
    expect(not passes(Rule("S", "tree-hit", trees=("src",), forbid=("export module",))), "tree fails")
    expect(passes(Rule("S", "tree-exclude", trees=("src",), exclude=("src/a/Other",),
                       forbid=("export module",))), "tree exclude")
    expect(passes(Rule("S", "owners", trees=("src",), owners=(("export module Unit", ("src/a/Other.cppm",)),))),
           "owners passes")
    expect(not passes(Rule("S", "owners-bad", trees=("src",), owners=(("export module Unit", ("src/a/Unit.cpp",)),))),
           "owners fails")
    expect(passes(Rule("S", "glue", files=unit, no_module_directive=True)), "textual glue passes")
    expect(not passes(Rule("S", "glue-bad", files=("src/a/Other.cppm",), no_module_directive=True)),
           "module directive fails")
    # Missing inputs fail closed instead of passing an empty text.
    expect(not passes(Rule("S", "missing", files=("src/a/Gone.cpp",), forbid=("x",))), "missing file fails")
    expect(not passes(Rule("S", "no-slice", files=unit, slice=("nope", "After"), forbid=("x",))),
           "missing slice fails")
    expect(not passes(Rule("S", "no-tree", trees=("lib",), forbid=("x",))), "missing tree fails")
    unreadable = Rule("S", "unreadable", trees=("src",), forbid=("x",))
    expect(checker.check_rule(unreadable, root, read=lambda path: None), "unreadable tree file fails")
    (root / "empty").mkdir()
    expect(not passes(Rule("S", "empty-tree", trees=("empty",), forbid=("x",))), "empty tree fails")

    # The self-check proves passing rules bite and reports a rule whose check is vacuous.
    # `Beta();` also occurs before the slice, so mutations must edit inside the slice.
    strong = [
        Rule("S", "strong", files=unit, slice=run, require=("Alpha();",), forbid=("Gamma",),
             order=("Alpha", "Beta"), count=(("Beta();", 2),),
             first_order=("Alpha();", "Beta();")),
        Rule("S", "strong-absent", absent=("src/a/Retired.cpp",)),
        Rule("S", "strong-tree", trees=("src",), forbid=("Epsilon",),
             owners=(("export module Unit", ("src/a/Other.cppm",)),)),
        Rule("S", "strong-glue", files=unit, no_module_directive=True),
    ]
    for rule in strong:
        expect(passes(rule), f"{rule.name} passes live, so its mutations are checked")
    expect(not checker.self_check(strong, root), f"self-check accepts {checker.self_check(strong, root)}")
    # An order over one repeated token cannot be broken by reordering.
    vacuous = [Rule("S", "vacuous", files=unit, order=("Beta();", "Beta();"))]
    expect(checker.self_check(vacuous, root), "self-check rejects a vacuous order")

    expect(checker._validate([Rule("S", "x", files=unit), Rule("S", "x", files=unit)]),
           "duplicate labels rejected")
    expect(checker._validate([Rule("S", "t", trees=("src",), require=("a",))]), "tree require rejected")

print("check_source_rules regressions: ok")
