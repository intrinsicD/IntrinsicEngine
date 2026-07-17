#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Sequence

SOURCE_EXTENSIONS = (".cppm", ".cpp", ".cc", ".cxx", ".c")
COMPILE_OUTPUT_SUFFIXES = (".cppm.o", ".cpp.o", ".cc.o", ".cxx.o", ".c.o", ".o", ".pcm")
SOURCE_ROOTS = ("src", "tests", "methods", "benchmarks")
DEPENDENCY_ROOTS = ("external", "third_party")
MIN_NINJA_LOG_VERSION = 4
MAX_NINJA_LOG_VERSION = 7
BASELINE_ROOT_FIELDS = frozenset({"max_regression_ms", "targets"})
BASELINE_TARGET_FIELDS = frozenset(
    {"edge_id", "source", "edge_kind", "outputs", "max_duration_ms"}
)
BASELINE_EDGE_KINDS = frozenset(
    {"module-interface", "module-implementation", "translation-unit"}
)


class AnalysisError(RuntimeError):
    pass


@dataclass(frozen=True)
class NinjaEntry:
    duration_ms: int
    output: str
    outputs: tuple[str, ...]
    start_ms: int
    end_ms: int
    command_hash: str
    log_version: int


def _absolute(root: Path, value: str | Path) -> Path:
    path = Path(value)
    return path if path.is_absolute() else root / path


def _normalize_output(output: str, directory: Path, build_dir: Path) -> str:
    path = Path(output)
    absolute = (path if path.is_absolute() else directory / path).resolve(
        strict=False
    )
    try:
        return absolute.relative_to(build_dir.resolve(strict=False)).as_posix()
    except ValueError:
        return absolute.as_posix()


def parse_ninja_log(
    path: Path,
    build_dir: Path | None = None,
) -> list[NinjaEntry]:
    build_dir = (build_dir or path.parent).resolve(strict=False)
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise AnalysisError(f"cannot read Ninja log {path}: {error}") from error
    header = lines[0].strip() if lines else ""
    signature = "# ninja log v"
    version_text = header.removeprefix(signature)
    if not header.startswith(signature) or not version_text.isdigit():
        raise AnalysisError(
            f"{path}: malformed Ninja log header {header or '<empty>'!r}"
        )
    log_version = int(version_text)
    if not MIN_NINJA_LOG_VERSION <= log_version <= MAX_NINJA_LOG_VERSION:
        raise AnalysisError(
            f"{path}: unsupported Ninja log version v{log_version}; "
            f"supported versions are v{MIN_NINJA_LOG_VERSION}-"
            f"v{MAX_NINJA_LOG_VERSION}"
        )

    # Ninja appends records across incremental builds. Resolve the latest record
    # per normalized output before grouping one command's .o/.pcm outputs.
    latest: dict[str, tuple[int, int, str]] = {}
    for line_number, line in enumerate(lines[1:], start=2):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t", 4) if log_version == 4 else line.split("\t")
        if len(parts) != 5:
            raise AnalysisError(
                f"{path}:{line_number}: expected five tab-separated fields"
            )
        start_text, end_text, mtime_text, raw_output, command_field = parts
        try:
            start_ms, end_ms, _ = (
                int(start_text),
                int(end_text),
                int(mtime_text),
            )
        except ValueError as error:
            raise AnalysisError(
                f"{path}:{line_number}: timing fields must be integers"
            ) from error
        if end_ms < start_ms or not raw_output or not command_field:
            raise AnalysisError(f"{path}:{line_number}: malformed Ninja record")
        if log_version == 4:
            command_hash = hashlib.sha256(command_field.encode()).hexdigest()
        else:
            if re.fullmatch(r"[0-9a-f]{1,16}", command_field) is None:
                raise AnalysisError(
                    f"{path}:{line_number}: command hash must be "
                    "1-16 lowercase hexadecimal digits"
                )
            command_hash = command_field
        output = _normalize_output(raw_output, build_dir, build_dir)
        if output.endswith((".o", ".pcm")):
            latest[output] = (start_ms, end_ms, command_hash)

    # Restat rewrites output mtimes independently, so mtime is validated above
    # but deliberately excluded from physical-command grouping.
    grouped: dict[tuple[int, int, str], list[str]] = defaultdict(list)
    for output, identity in latest.items():
        grouped[identity].append(output)
    entries: list[NinjaEntry] = []
    for (start_ms, end_ms, command_hash), outputs in grouped.items():
        physical_outputs = tuple(sorted(outputs))
        primary = next(
            (output for output in physical_outputs if output.endswith(".o")),
            physical_outputs[0],
        )
        entries.append(
            NinjaEntry(
                duration_ms=end_ms - start_ms,
                output=primary,
                outputs=physical_outputs,
                start_ms=start_ms,
                end_ms=end_ms,
                command_hash=command_hash,
                log_version=log_version,
            )
        )
    return entries


def strip_compile_suffix(path_fragment: str) -> tuple[str, str | None]:
    for suffix in COMPILE_OUTPUT_SUFFIXES:
        if path_fragment.endswith(suffix):
            extension = {
                ".cppm.o": ".cppm",
                ".cpp.o": ".cpp",
                ".cc.o": ".cc",
                ".cxx.o": ".cxx",
                ".c.o": ".c",
            }.get(suffix)
            return path_fragment[: -len(suffix)], extension
    return path_fragment, None


class SourceResolver:
    """Resolve Ninja outputs through the configured compile-command graph."""

    def __init__(
        self,
        repo_root: Path,
        build_dir: Path | None = None,
        compile_commands_path: Path | None = None,
    ) -> None:
        self.repo_root = repo_root.resolve(strict=False)
        self.build_dir = build_dir.resolve(strict=False) if build_dir else None
        self.sources_by_root = {
            root: tuple(
                sorted(
                    path.relative_to(self.repo_root).as_posix()
                    for path in (self.repo_root / root).rglob("*")
                    if path.is_file() and path.suffix in SOURCE_EXTENSIONS
                )
            )
            for root in SOURCE_ROOTS
        }
        self.sources = tuple(
            source
            for root in SOURCE_ROOTS
            for source in self.sources_by_root[root]
        )
        self.by_output: dict[str, tuple[tuple[str, str, str | None], ...]] = {}
        self.configured_root_counts: Counter[str] = Counter()
        if self.build_dir is not None:
            self._load_compile_commands(
                compile_commands_path
                or self.build_dir / "compile_commands.json"
            )

    def _classify(
        self,
        raw_source: str,
        directory: Path,
    ) -> tuple[str, str, str | None]:
        source = Path(raw_source)
        source = (source if source.is_absolute() else directory / source).resolve(
            strict=False
        )
        assert self.build_dir is not None
        try:
            return (
                f"@build/{source.relative_to(self.build_dir).as_posix()}",
                "generated",
                None,
            )
        except ValueError:
            pass
        try:
            relative = source.relative_to(self.repo_root)
        except ValueError:
            return source.as_posix(), "external", None
        root = relative.parts[0] if relative.parts else ""
        if root in SOURCE_ROOTS:
            status = "repository"
        elif root in DEPENDENCY_ROOTS:
            status = "dependency"
        else:
            status = "outside-declared-roots"
        return relative.as_posix(), status, root if status == "repository" else None

    def _load_compile_commands(self, path: Path) -> None:
        try:
            commands = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise AnalysisError(f"cannot read compile commands {path}: {error}") from error
        if not isinstance(commands, list):
            raise AnalysisError(f"{path}: compile commands root must be an array")
        mapped: dict[str, set[tuple[str, str, str | None]]] = defaultdict(set)
        for index, command in enumerate(commands):
            if not isinstance(command, dict):
                raise AnalysisError(f"{path}: entry {index} must be an object")
            values = [command.get(key) for key in ("directory", "file", "output")]
            if not all(isinstance(value, str) and value for value in values):
                raise AnalysisError(
                    f"{path}: entry {index} requires directory, file, and output"
                )
            directory = Path(values[0])
            directory = (
                directory
                if directory.is_absolute()
                else self.build_dir / directory
            ).resolve(strict=False)
            output = _normalize_output(values[2], directory, self.build_dir)
            candidate = self._classify(values[1], directory)
            mapped[output].add(candidate)
        self.by_output = {
            output: tuple(sorted(candidates))
            for output, candidates in mapped.items()
        }
        self.configured_root_counts.update(
            root
            for candidates in self.by_output.values()
            for _, status, root in candidates
            if status == "repository" and root is not None
        )

    def _diagnostic_candidates(self, outputs: Iterable[str]) -> list[str]:
        matches: set[str] = set()
        for output in outputs:
            fragment = output.split(".dir/", 1)[1] if ".dir/" in output else Path(output).name
            stem, preferred_extension = strip_compile_suffix(fragment)
            output_matches: set[str] = set()
            for token in (stem, Path(stem).name):
                suffixes = (
                    (token,)
                    if token.endswith(SOURCE_EXTENSIONS)
                    else tuple(f"{token}{extension}" for extension in SOURCE_EXTENSIONS)
                )
                output_matches.update(
                    source
                    for source in self.sources
                    if any(source.endswith(suffix) for suffix in suffixes)
                )
            preferred = {
                source
                for source in output_matches
                if preferred_extension and source.endswith(preferred_extension)
            }
            matches.update(preferred or output_matches)
        return sorted(matches)

    def resolve_outputs(self, outputs: Iterable[str]) -> dict[str, object]:
        outputs = tuple(outputs)
        mapped_outputs = [output for output in outputs if output in self.by_output]
        candidates = {
            candidate
            for output in mapped_outputs
            for candidate in self.by_output[output]
        }
        if len(candidates) == 1:
            source, source_status, root = next(iter(candidates))
            status = "resolved" if source_status == "repository" else source_status
            return {
                "source": source,
                "source_root": root,
                "status": status,
                "reason": "compile-command-output",
                "mapped_outputs": sorted(mapped_outputs),
                "candidates": [source],
            }
        if candidates:
            return {
                "source": None,
                "source_root": None,
                "status": "ambiguous",
                "reason": "compile-command-output-maps-to-multiple-sources",
                "mapped_outputs": sorted(mapped_outputs),
                "candidates": sorted(source for source, _, _ in candidates),
            }
        return {
            "source": None,
            "source_root": None,
            "status": "unresolved",
            "reason": "output-missing-from-current-compile-commands",
            "mapped_outputs": [],
            "candidates": self._diagnostic_candidates(outputs),
        }

    def resolve(self, output: str) -> str | None:
        """Retain the old single-output API without guessing ambiguities."""
        resolution = self.resolve_outputs((output,))
        if resolution["status"] == "resolved":
            return str(resolution["source"])
        if self.build_dir is None:
            candidates = self._diagnostic_candidates((output,))
            return candidates[0] if len(candidates) == 1 else None
        return None


def source_stats(root: Path, rel_source: str) -> tuple[int, int, int, int] | None:
    path = root / rel_source
    if not path.exists():
        return None
    lines = path.read_text(errors="ignore").splitlines()
    return (
        len(lines),
        sum(line.strip().startswith("#include") for line in lines),
        sum(line.strip().startswith("import ") for line in lines),
        sum(line.strip().startswith("export ") for line in lines),
    )


def _edge_kind(source: object, status: str, outputs: Iterable[str]) -> str:
    if status != "resolved" or not isinstance(source, str):
        return status
    if source.endswith(".cppm"):
        return "module-interface"
    return (
        "module-implementation"
        if any(output.endswith(".pcm") for output in outputs)
        else "translation-unit"
    )


def build_report_row(
    root: Path,
    resolver: SourceResolver,
    entry: NinjaEntry,
) -> dict[str, object]:
    resolution = resolver.resolve_outputs(entry.outputs)
    source = resolution["source"]
    stats = None
    if resolution["status"] == "resolved" and isinstance(source, str):
        stats = source_stats(root, source)
        if stats is None:
            resolution.update(
                status="unresolved",
                reason="resolved-repository-source-is-missing",
            )
    edge_kind = _edge_kind(source, str(resolution["status"]), entry.outputs)
    edge_identity = json.dumps(
        [source, edge_kind, entry.outputs],
        separators=(",", ":"),
    )
    row: dict[str, object] = {
        "edge_id": hashlib.sha256(edge_identity.encode()).hexdigest(),
        "physical_identity": {
            "start_ms": entry.start_ms,
            "end_ms": entry.end_ms,
            "command_hash": entry.command_hash,
            "ninja_log_version": entry.log_version,
        },
        "duration_ms": entry.duration_ms,
        "output": entry.output,
        "outputs": list(entry.outputs),
        "edge_kind": edge_kind,
        "source": source,
        "source_root": resolution.pop("source_root"),
        "resolution": resolution,
        "source_lines": None,
        "includes": None,
        "imports": None,
        "exports": None,
    }
    if stats is None:
        return row
    (
        row["source_lines"],
        row["includes"],
        row["imports"],
        row["exports"],
    ) = stats
    return row


def analyze_build(repo_root: Path, build_dir: Path) -> dict[str, object]:
    repo_root, build_dir = repo_root.resolve(), build_dir.resolve()
    resolver = SourceResolver(repo_root, build_dir)
    rows = [
        build_report_row(repo_root, resolver, entry)
        for entry in parse_ninja_log(build_dir / ".ninja_log", build_dir)
    ]
    rows.sort(key=lambda row: (-int(row["duration_ms"]), str(row["edge_id"])))
    edge_ids = Counter(str(row["edge_id"]) for row in rows)
    collisions = sorted(edge_id for edge_id, count in edge_ids.items() if count > 1)
    if collisions:
        raise AnalysisError(
            "stable compile-edge identity collision: " + ", ".join(collisions)
        )
    status_counts = Counter(str(row["resolution"]["status"]) for row in rows)
    compiled_roots = Counter(
        str(row["source_root"])
        for row in rows
        if row["resolution"]["status"] == "resolved"
    )
    issues = [
        row
        for row in rows
        if row["resolution"]["status"]
        in {"ambiguous", "outside-declared-roots", "unresolved"}
    ]
    unresolved = [
        row
        for row in issues
        if row["resolution"]["status"] in {"ambiguous", "unresolved"}
    ]
    return {
        "schema": "intrinsic.compile-hotspots/v2",
        "source_roots": [
            {
                "root": root,
                "indexed_source_count": len(resolver.sources_by_root[root]),
                "configured_command_count": resolver.configured_root_counts[root],
                "compiled_edge_count": compiled_roots[root],
                "present_in_configured_graph": resolver.configured_root_counts[root] > 0,
                "present_in_sampled_build": compiled_roots[root] > 0,
            }
            for root in SOURCE_ROOTS
        ],
        "summary": {
            "latest_compile_output_count": sum(len(row["outputs"]) for row in rows),
            "physical_compile_edge_count": len(rows),
            "multi_output_edge_count": sum(len(row["outputs"]) > 1 for row in rows),
            **{
                f"{status.replace('-', '_')}_edge_count": status_counts[status]
                for status in (
                    "resolved",
                    "generated",
                    "dependency",
                    "external",
                    "outside-declared-roots",
                    "ambiguous",
                    "unresolved",
                )
            },
            "unresolved_output_count": sum(len(row["outputs"]) for row in unresolved),
            "resolution_issue_output_count": sum(len(row["outputs"]) for row in issues),
        },
        "edges": rows,
        "resolution_issues": issues,
    }


def _require_exact_fields(
    record: Mapping[str, object],
    required: frozenset[str],
    allowed: frozenset[str],
    context: str,
) -> None:
    missing = sorted(required - record.keys())
    unknown = sorted(record.keys() - allowed)
    if missing:
        raise AnalysisError(f"{context} omits required fields {missing!r}")
    if unknown:
        raise AnalysisError(f"{context} contains unknown fields {unknown!r}")


def _require_nonnegative_integer(value: object, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise AnalysisError(f"{context} must be a nonnegative integer")
    return value


def _validate_baseline(
    baseline: Mapping[str, object],
) -> tuple[int, list[Mapping[str, object]]]:
    if not isinstance(baseline, dict):
        raise AnalysisError("baseline root must be an object")
    _require_exact_fields(
        baseline,
        BASELINE_ROOT_FIELDS,
        BASELINE_ROOT_FIELDS,
        "baseline root",
    )
    tolerance = _require_nonnegative_integer(
        baseline["max_regression_ms"],
        "baseline max_regression_ms",
    )
    targets = baseline["targets"]
    if not isinstance(targets, list):
        raise AnalysisError("baseline targets must be an array")
    if not targets:
        raise AnalysisError("baseline targets must contain at least one target")
    validated_targets: list[Mapping[str, object]] = []
    for index, target in enumerate(targets):
        context = f"baseline target {index}"
        if not isinstance(target, dict):
            raise AnalysisError(f"{context} must be an object")
        _require_exact_fields(
            target,
            frozenset({"max_duration_ms"}),
            BASELINE_TARGET_FIELDS,
            context,
        )
        _require_nonnegative_integer(
            target["max_duration_ms"],
            f"{context} max_duration_ms",
        )
        has_edge_id = "edge_id" in target
        has_source = "source" in target
        if not has_edge_id and not has_source:
            raise AnalysisError(f"{context} requires edge_id or source")
        if has_edge_id:
            edge_id = target["edge_id"]
            if (
                not isinstance(edge_id, str)
                or re.fullmatch(r"[0-9a-f]{64}", edge_id) is None
            ):
                raise AnalysisError(
                    f"{context} edge_id must be 64 lowercase hexadecimal digits"
                )
        if has_source:
            source = target["source"]
            if not isinstance(source, str) or not source:
                raise AnalysisError(f"{context} source must be a non-empty string")
        if "edge_kind" in target:
            edge_kind = target["edge_kind"]
            if (
                not isinstance(edge_kind, str)
                or edge_kind not in BASELINE_EDGE_KINDS
            ):
                raise AnalysisError(
                    f"{context} edge_kind must be one of "
                    f"{sorted(BASELINE_EDGE_KINDS)!r}"
                )
        if "outputs" in target:
            outputs = target["outputs"]
            if (
                not isinstance(outputs, list)
                or not outputs
                or any(not isinstance(output, str) or not output for output in outputs)
            ):
                raise AnalysisError(
                    f"{context} outputs must be a non-empty array of strings"
                )
            if outputs != sorted(set(outputs)):
                raise AnalysisError(
                    f"{context} outputs must be sorted and contain no duplicates"
                )
        validated_targets.append(target)
    return tolerance, validated_targets


def compare_baseline(
    rows: Sequence[Mapping[str, object]],
    baseline: Mapping[str, object],
) -> list[str]:
    tolerance, targets = _validate_baseline(baseline)
    by_id = {str(row["edge_id"]): row for row in rows}
    by_source: dict[str, list[Mapping[str, object]]] = defaultdict(list)
    for row in rows:
        source = row["source"]
        if isinstance(source, str) and source:
            by_source[source].append(row)
    if len(by_id) != len(rows):
        raise AnalysisError("duplicate edge_id in compile report")

    failures: list[str] = []
    seen: set[tuple[str, str]] = set()
    for index, target in enumerate(targets):
        edge_id, source = target.get("edge_id"), target.get("source")
        if edge_id is not None:
            key = ("edge_id", edge_id)
            row = by_id.get(edge_id)
            if row is None:
                failures.append(f"missing compile edge_id '{edge_id}'")
                continue
        else:
            assert isinstance(source, str)
            key = ("source", source)
            matches = by_source.get(source, [])
            if len(matches) != 1:
                qualifier = "missing" if not matches else "ambiguous"
                detail = (
                    ""
                    if not matches
                    else " maps to edge_ids "
                    + ", ".join(str(match["edge_id"]) for match in matches)
                )
                failures.append(f"{qualifier} source '{source}'{detail}")
                continue
            row = matches[0]
        if key in seen:
            failures.append(f"duplicate baseline target {key[0]} '{key[1]}'")
            continue
        seen.add(key)
        status = row["resolution"]["status"]
        if status != "resolved":
            failures.append(
                f"compile {key[0]} '{key[1]}' is not baseline-eligible: "
                f"resolution status is {status!r}"
            )
            continue
        if source is not None and row["source"] != source:
            failures.append(
                f"edge_id '{edge_id}' source mismatch: "
                f"expected {source!r}, got {row['source']!r}"
            )
            continue
        for field in ("edge_kind", "outputs"):
            if field in target and row[field] != target[field]:
                failures.append(
                    f"{key[1]}: {field} mismatch; expected "
                    f"{target[field]!r}, got {row[field]!r}"
                )
                break
        else:
            max_duration_ms = target["max_duration_ms"]
            assert isinstance(max_duration_ms, int)
            limit = max_duration_ms + tolerance
            if int(row["duration_ms"]) > limit:
                failures.append(
                    f"{key[1]}: {row['duration_ms']}ms exceeds budget "
                    f"{target['max_duration_ms']}ms (+{tolerance}ms tolerance)"
                )
    return failures


def _repository_owned_rows(
    rows: Sequence[Mapping[str, object]],
) -> list[Mapping[str, object]]:
    return [
        row
        for row in rows
        if row["resolution"]["status"] == "resolved"
    ]


def _print_rows(rows: Sequence[Mapping[str, object]], log_path: Path) -> None:
    print(
        f"Top {len(rows)} repository-owned physical compile edges "
        f"from {log_path}:"
    )
    print("duration_s\toutput\tsource\tsource_lines\tincludes\timports\texports")
    for row in rows:
        values = [
            row[key] if row[key] is not None else "-"
            for key in ("source", "source_lines", "includes", "imports", "exports")
        ]
        print(
            f"{int(row['duration_ms']) / 1000:.3f}\t{row['output']}\t"
            + "\t".join(str(value) for value in values)
        )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Report slow normalized Ninja compile/module edges"
    )
    parser.add_argument("--build-dir", default="build/ci")
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--json-out")
    parser.add_argument("--baseline-json")
    args = parser.parse_args(argv)
    if args.top < 1:
        parser.error("--top must be positive")

    root = Path(__file__).resolve().parents[2]
    build_dir = _absolute(root, args.build_dir)
    try:
        report = analyze_build(root, build_dir)
    except AnalysisError as error:
        print(f"Compile hotspot analysis failed: {error}")
        return 2
    ranked_rows = _repository_owned_rows(report["edges"])
    rows = ranked_rows[: args.top]
    report.update(build_dir=args.build_dir, top=args.top, rows=rows)
    _print_rows(rows, build_dir / ".ninja_log")
    print(
        f"Physical edges: {report['summary']['physical_compile_edge_count']}; "
        f"resolution issues: {len(report['resolution_issues'])}."
    )

    if args.json_out:
        output = _absolute(root, args.json_out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Wrote JSON report: {output}")
    if args.baseline_json:
        try:
            baseline = json.loads(
                _absolute(root, args.baseline_json).read_text(encoding="utf-8")
            )
            if not isinstance(baseline, dict):
                raise AnalysisError("baseline root must be an object")
            failures = compare_baseline(report["edges"], baseline)
        except (OSError, json.JSONDecodeError, AnalysisError) as error:
            print(f"Compile hotspot baseline check failed: {error}")
            return 2
        if failures:
            print("Compile hotspot baseline check failed:")
            for failure in failures:
                print(f"  - {failure}")
            return 2
        print(
            f"Compile hotspot baseline check passed "
            f"({len(baseline.get('targets', []))} targets)."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
