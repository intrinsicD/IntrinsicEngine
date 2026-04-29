#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re

TIER_AB_MODULES = (
    "src/Runtime/Runtime.RenderOrchestrator.cppm",
    "src/Runtime/Graphics/Graphics.RenderDriver.cppm",
    "src/Runtime/Runtime.GraphicsBackend.cppm",
    "src/Runtime/Runtime.AssetPipeline.cppm",
    "src/Runtime/Graphics/Graphics.PipelineLibrary.cppm",
)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".cppm", ".ixx"}
BASELINE_ROW_RE = re.compile(
    r"^\| `(?P<file>[^`]+)` \| (?P<lines>\d+) \| (?P<imports>\d+) \| (?P<includes>\d+) \| (?P<exports>\d+) \|$"
)


@dataclass(frozen=True)
class FanoutStats:
    file: str
    includes: int
    imports: int
    exports: int
    lines: int


def compute_stats(path: Path, repo_root: Path) -> FanoutStats:
    text = path.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines()
    includes = sum(1 for line in lines if line.strip().startswith("#include "))
    imports = sum(1 for line in lines if line.strip().startswith("import "))
    exports = sum(1 for line in lines if line.strip().startswith("export "))
    return FanoutStats(
        file=path.relative_to(repo_root).as_posix(),
        includes=includes,
        imports=imports,
        exports=exports,
        lines=len(lines),
    )


def discover_source_files(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*") if path.is_file() and path.suffix in SOURCE_SUFFIXES)


def read_baseline(path: Path) -> dict[str, FanoutStats]:
    if not path.exists():
        return {}

    baseline: dict[str, FanoutStats] = {}
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = BASELINE_ROW_RE.match(line.strip())
        if not match:
            continue
        baseline[match.group("file")] = FanoutStats(
            file=match.group("file"),
            lines=int(match.group("lines")),
            imports=int(match.group("imports")),
            includes=int(match.group("includes")),
            exports=int(match.group("exports")),
        )
    return baseline


def main() -> int:
    parser = argparse.ArgumentParser(description="Report module fan-out (imports/includes/exports) for selected interfaces")
    parser.add_argument("--output", help="Optional output markdown path")
    parser.add_argument("--tier-a-b", action="store_true", help="Use the PImpl TODO Tier A/B module set")
    parser.add_argument("--root", help="Scan all source files under this root relative to the repository root")
    parser.add_argument(
        "--fail-on-regression",
        action="store_true",
        help="Fail if a file that exists in the baseline has higher import/include/export fan-out",
    )
    parser.add_argument(
        "--baseline",
        default="tools/analysis/module_fanout_baseline_2026-04-03.md",
        help="Baseline markdown table used by --fail-on-regression",
    )
    parser.add_argument("files", nargs="*", help="Module interface files (relative to repo root)")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    selected_files = list(args.files)
    if args.root:
        root = Path(args.root)
        if not root.is_absolute():
            root = repo_root / root
        selected_files.extend(path.relative_to(repo_root).as_posix() for path in discover_source_files(root))
    if args.tier_a_b:
        selected_files.extend(TIER_AB_MODULES)
    if not selected_files:
        raise SystemExit("No files selected. Pass file paths or --tier-a-b.")

    # Stable order, no duplicates.
    selected_files = list(dict.fromkeys(selected_files))
    stats = [compute_stats(repo_root / rel, repo_root) for rel in selected_files]
    regressions: list[str] = []
    if args.fail_on_regression:
        baseline_path = Path(args.baseline)
        if not baseline_path.is_absolute():
            baseline_path = repo_root / baseline_path
        baseline = read_baseline(baseline_path)
        for current in stats:
            previous = baseline.get(current.file)
            if not previous:
                continue
            if (
                current.imports > previous.imports
                or current.includes > previous.includes
                or current.exports > previous.exports
            ):
                regressions.append(
                    f"{current.file}: imports {previous.imports}->{current.imports}, "
                    f"includes {previous.includes}->{current.includes}, exports {previous.exports}->{current.exports}"
                )

    table_header = "| File | Lines | `import` | `#include` | `export` |"
    table_sep = "|---|---:|---:|---:|---:|"

    lines: list[str] = []
    lines.append("# Module Fan-out Report")
    lines.append("")
    lines.append(table_header)
    lines.append(table_sep)
    for s in stats:
        lines.append(f"| `{s.file}` | {s.lines} | {s.imports} | {s.includes} | {s.exports} |")

    total_imports = sum(s.imports for s in stats)
    total_includes = sum(s.includes for s in stats)
    total_exports = sum(s.exports for s in stats)
    lines.append(f"| **Total** |  | **{total_imports}** | **{total_includes}** | **{total_exports}** |")
    lines.append("")

    output = "\n".join(lines)
    print(output)
    if args.fail_on_regression:
        if regressions:
            print("\nFan-out regressions detected:")
            for regression in regressions:
                print(f"- {regression}")
            return 1
        print("\nNo fan-out regressions detected for files present in the baseline.")

    if args.output:
        output_path = Path(args.output)
        if not output_path.is_absolute():
            output_path = repo_root / output_path
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(output + "\n", encoding="utf-8")
        print(f"\nWrote report: {output_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
