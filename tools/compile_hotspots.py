#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

SOURCE_EXTENSIONS = (".cppm", ".cpp", ".cc", ".cxx", ".c")
COMPILE_OUTPUT_SUFFIXES = (".cppm.o", ".cpp.o", ".cc.o", ".cxx.o", ".c.o", ".o", ".pcm")


@dataclass
class NinjaEntry:
    duration_ms: int
    output: str


def parse_ninja_log(path: Path) -> list[NinjaEntry]:
    # Ninja logs may contain multiple records per output across incremental builds.
    # Keep only the most recent record for each output (last-write-wins by file order).
    latest_by_output: dict[str, NinjaEntry] = {}

    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 5:
            continue
        start, end, _, output, _ = parts[:5]
        try:
            duration_ms = int(end) - int(start)
        except ValueError:
            continue
        latest_by_output[output] = NinjaEntry(duration_ms=duration_ms, output=output)
    return list(latest_by_output.values())


def strip_compile_suffix(path_fragment: str) -> tuple[str, str | None]:
    for suffix in COMPILE_OUTPUT_SUFFIXES:
        if path_fragment.endswith(suffix):
            stem = path_fragment[: -len(suffix)]
            if suffix == ".cppm.o":
                return stem, ".cppm"
            if suffix == ".cpp.o":
                return stem, ".cpp"
            if suffix == ".cc.o":
                return stem, ".cc"
            if suffix == ".cxx.o":
                return stem, ".cxx"
            if suffix == ".c.o":
                return stem, ".c"
            return stem, None
    return path_fragment, None


class SourceResolver:
    def __init__(self, repo_root: Path) -> None:
        self.repo_root = repo_root
        self.sources = [p.relative_to(repo_root).as_posix() for p in repo_root.glob("src/**/*") if p.suffix in SOURCE_EXTENSIONS]

    def _choose_best(self, candidates: list[str], preferred_ext: str | None) -> str | None:
        if not candidates:
            return None
        if preferred_ext:
            exact = [c for c in candidates if c.endswith(preferred_ext)]
            if exact:
                return sorted(exact, key=len)[0]
        return sorted(candidates, key=len)[0]

    def resolve(self, output: str) -> str | None:
        fragment = output.split(".dir/", 1)[1] if ".dir/" in output else Path(output).name
        stem, preferred_ext = strip_compile_suffix(fragment)

        tokens = [stem, stem.replace("-", ".")]
        if "/" in stem:
            basename = Path(stem).name
            tokens.extend([basename, basename.replace("-", ".")])

        candidates: list[str] = []
        for token in tokens:
            if not token:
                continue
            # direct match if token already contains extension
            if any(token.endswith(ext) for ext in SOURCE_EXTENSIONS):
                direct = [s for s in self.sources if s.endswith(token)]
                candidates.extend(direct)
                continue

            # extension-aware probing
            for ext in SOURCE_EXTENSIONS:
                target = f"{token}{ext}"
                matches = [s for s in self.sources if s.endswith(target)]
                candidates.extend(matches)

            # module-impl artifacts like ECS-Scene.Impl.pcm -> ECS.Scene.cpp(.m)
            if token.endswith(".Impl"):
                base = token[: -len(".Impl")]
                for ext in SOURCE_EXTENSIONS:
                    target = f"{base}{ext}"
                    matches = [s for s in self.sources if s.endswith(target)]
                    candidates.extend(matches)

        unique = list(dict.fromkeys(candidates))
        return self._choose_best(unique, preferred_ext)


def build_report_row(root: Path, resolver: SourceResolver, entry: NinjaEntry) -> dict[str, object]:
    rel_source = resolver.resolve(entry.output)
    if rel_source is None:
        return {
            "duration_ms": entry.duration_ms,
            "output": entry.output,
            "source": None,
            "source_lines": None,
            "includes": None,
            "imports": None,
            "exports": None,
        }

    stats = source_stats(root, rel_source)
    if stats is None:
        return {
            "duration_ms": entry.duration_ms,
            "output": entry.output,
            "source": rel_source,
            "source_lines": None,
            "includes": None,
            "imports": None,
            "exports": None,
        }

    line_count, include_count, import_count, export_count = stats
    return {
        "duration_ms": entry.duration_ms,
        "output": entry.output,
        "source": rel_source,
        "source_lines": line_count,
        "includes": include_count,
        "imports": import_count,
        "exports": export_count,
    }


def source_stats(root: Path, rel_source: str) -> tuple[int, int, int, int] | None:
    p = root / rel_source
    if not p.exists():
        return None
    lines = p.read_text(errors="ignore").splitlines()
    include_count = sum(1 for ln in lines if ln.strip().startswith("#include"))
    import_count = sum(1 for ln in lines if ln.strip().startswith("import "))
    export_count = sum(1 for ln in lines if ln.strip().startswith("export "))
    return len(lines), include_count, import_count, export_count


def iter_compile_entries(entries: Iterable[NinjaEntry]) -> Iterable[NinjaEntry]:
    for entry in entries:
        if entry.output.endswith((".o", ".pcm")):
            yield entry


def main() -> int:
    parser = argparse.ArgumentParser(description="Report slow Ninja compile/module edges")
    parser.add_argument("--build-dir", default="build/ci", help="Build directory that owns .ninja_log")
    parser.add_argument("--top", type=int, default=15, help="Number of rows to print")
    parser.add_argument("--json-out", help="Optional path to write the top-edge report as JSON")
    parser.add_argument(
        "--baseline-json",
        help=(
            "Optional baseline JSON for CI comparison. "
            "Schema: {\"max_regression_ms\": int, \"targets\": [{\"source\": str, \"max_duration_ms\": int}]}"
        ),
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    log_path = root / args.build_dir / ".ninja_log"
    if not log_path.exists():
        raise SystemExit(f"Missing ninja log: {log_path}")

    resolver = SourceResolver(root)
    entries = sorted(iter_compile_entries(parse_ninja_log(log_path)), key=lambda x: x.duration_ms, reverse=True)

    all_rows = [build_report_row(root, resolver, entry) for entry in entries]
    report_rows = all_rows[: args.top]

    print(f"Top {min(args.top, len(entries))} compile edges from {log_path}:")
    print("duration_s\toutput\tsource\tsource_lines\tincludes\timports\texports")
    for row in report_rows:
        source = row["source"] if row["source"] is not None else "-"
        source_lines = row["source_lines"] if row["source_lines"] is not None else "-"
        includes = row["includes"] if row["includes"] is not None else "-"
        imports = row["imports"] if row["imports"] is not None else "-"
        exports = row["exports"] if row["exports"] is not None else "-"
        print(
            f"{int(row['duration_ms'])/1000:.3f}\t{row['output']}\t{source}\t{source_lines}\t{includes}\t{imports}\t{exports}"
        )

    if args.json_out:
        out_path = (root / args.json_out) if not Path(args.json_out).is_absolute() else Path(args.json_out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        payload = {"build_dir": args.build_dir, "top": args.top, "rows": report_rows}
        out_path.write_text(json.dumps(payload, indent=2))
        print(f"Wrote JSON report: {out_path}")

    if args.baseline_json:
        baseline_path = (root / args.baseline_json) if not Path(args.baseline_json).is_absolute() else Path(args.baseline_json)
        baseline = json.loads(baseline_path.read_text())
        max_regression_ms = int(baseline.get("max_regression_ms", 0))
        targets = baseline.get("targets", [])
        row_by_source = {str(row.get("source")): row for row in all_rows if row.get("source")}

        failures: list[str] = []
        for target in targets:
            source = str(target["source"])
            max_duration_ms = int(target["max_duration_ms"])
            row = row_by_source.get(source)
            if row is None:
                failures.append(f"missing source '{source}' in compile entries")
                continue
            duration_ms = int(row["duration_ms"])
            if duration_ms > max_duration_ms + max_regression_ms:
                failures.append(
                    f"{source}: {duration_ms}ms exceeds budget {max_duration_ms}ms (+{max_regression_ms}ms tolerance)"
                )

        if failures:
            print("Compile hotspot baseline check failed:")
            for failure in failures:
                print(f"  - {failure}")
            return 2
        print(f"Compile hotspot baseline check passed ({len(targets)} targets).")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
