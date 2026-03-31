#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass
class NinjaEntry:
    duration_ms: int
    output: str


def parse_ninja_log(path: Path) -> list[NinjaEntry]:
    entries: list[NinjaEntry] = []
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
        entries.append(NinjaEntry(duration_ms=duration_ms, output=output))
    return entries


def normalize_source_path(output: str) -> str | None:
    if ".dir/" in output:
        inner = output.split('.dir/', 1)[1]
        for suffix in ('.cppm.o', '.cpp.o', '.cc.o', '.cxx.o', '.c.o', '.pcm'):
            if inner.endswith(suffix):
                candidate = inner[: -len(suffix)]
                if candidate.startswith('Systems/') or candidate.startswith('Components/'):
                    return f"src/Runtime/ECS/{candidate}.cpp"
                if candidate.startswith('Geometry.'):
                    return f"src/Runtime/Geometry/{candidate}.cppm"
                if candidate.startswith('ECS.'):
                    return f"src/Runtime/ECS/{candidate}.cpp"
                if candidate.startswith('Core.'):
                    return f"src/Core/{candidate}.cppm"
                if candidate.startswith('Runtime.'):
                    return f"src/Runtime/{candidate}.cpp"
                return None

    suffixes = [".cppm.o", ".cpp.o", ".cc.o", ".cxx.o", ".c.o", ".o", ".pcm"]
    stem = None
    for suffix in suffixes:
        if output.endswith(suffix):
            stem = Path(output).name[: -len(suffix)]
            break
    if stem is None:
        return None

    stem = stem.replace("-", ".")
    if stem.startswith("Geometry"):
        return f"src/Runtime/Geometry/{stem}.cppm"
    return None



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
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    log_path = root / args.build_dir / ".ninja_log"
    if not log_path.exists():
        raise SystemExit(f"Missing ninja log: {log_path}")

    entries = sorted(iter_compile_entries(parse_ninja_log(log_path)), key=lambda x: x.duration_ms, reverse=True)

    print(f"Top {min(args.top, len(entries))} compile edges from {log_path}:")
    print("duration_s\toutput\tsource_lines\tincludes\timports\texports")

    for entry in entries[: args.top]:
        rel_source = normalize_source_path(entry.output)
        if rel_source is None:
            print(f"{entry.duration_ms/1000:.3f}\t{entry.output}\t-\t-\t-\t-")
            continue

        source_rel = rel_source
        stats = source_stats(root, source_rel)
        if stats is None:
            print(f"{entry.duration_ms/1000:.3f}\t{entry.output}\t(n/a)\t-\t-\t-")
            continue

        line_count, include_count, import_count, export_count = stats
        print(
            f"{entry.duration_ms/1000:.3f}\t{entry.output}\t{line_count}\t{include_count}\t{import_count}\t{export_count}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
