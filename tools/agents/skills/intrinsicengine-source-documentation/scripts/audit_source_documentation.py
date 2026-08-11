#!/usr/bin/env python3
"""Audit IntrinsicEngine source synopses, comments, and README hygiene."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path

INTERFACE_SUFFIXES = frozenset({".cppm", ".h", ".hh", ".hpp", ".hxx"})
CPP_SUFFIXES = INTERFACE_SUFFIXES | frozenset({".c", ".cc", ".cpp", ".cxx"})
EXCLUDED_DIRECTORY_NAMES = frozenset(
    {
        ".cache",
        ".claude",
        ".codex",
        ".git",
        ".idea",
        ".mypy_cache",
        ".pytest_cache",
        ".ruff_cache",
        ".venv",
        "__pycache__",
        "external",
        "third_party",
        "venv",
    }
)

README_HISTORY_HEADING_RE = re.compile(
    r"\b(?:change\s*log|completed|future(?:\s+work)?|history|"
    r"historical(?:\s+notes?)?|migration\s+(?:history|journal|notes?)|"
    r"planned(?:\s+work)?|previous\s+versions?|roadmap|timeline|"
    r"retired(?:\s+(?:behavior|work))?)\b|^migration$",
    re.IGNORECASE,
)
HISTORICAL_LANGUAGE_RE = re.compile(
    r"\b(?:as\s+of\s+\d{4}|completed\s+(?:by|in)|formerly|historically|"
    r"landed\s+(?:in|as)|legacy\s+slice|no\s+longer|not\s+yet\s+implemented|"
    r"previously|retired|used\s+to)\b|\b(?:future|follow[- ]up|planned)\s+(?:task|work|slice)\b",
    re.IGNORECASE,
)
TASK_REFERENCE_RE = re.compile(r"\b(?:ADR-?\d{3,4}|[A-Z][A-Z0-9]{1,15}-\d{3,}[A-Z]?)\b")
LICENSE_RE = re.compile(
    r"\b(?:copyright|license[dn]?|spdx-license-identifier)\b", re.IGNORECASE
)
WEAK_SYNOPSIS_RE = re.compile(
    r"^(?:(?:this\s+)?(?:file|header|module)\s+)?(?:contains|declares|defines)\s+"
    r"(?:the\s+)?[A-Za-z_][A-Za-z0-9_:<>.-]*(?:\s+(?:file|header|module))?\.?$",
    re.IGNORECASE,
)
TYPE_DECLARATION_RE = re.compile(
    r"^(?:export\s+)?(?:template\s*<[^;{}]*>\s*)?"
    r"(?:struct|class|enum(?:\s+class)?)\s+[A-Za-z_]"
)
FUNCTION_DECLARATION_RE = re.compile(
    r"^(?:export\s+)?(?:template\s*<[^;{}]*>\s*)?"
    r"(?:\[\[[^]]+\]\]\s*)?"
    r"(?:(?:constexpr|consteval|constinit|explicit|friend|inline|static|virtual)\s+)*"
    r"[A-Za-z_][A-Za-z0-9_:<>,\s&*]*\s+[A-Za-z_~][A-Za-z0-9_:~]*\s*\("
)


@dataclass(frozen=True)
class CommentBlock:
    start: int
    end: int
    lines: tuple[str, ...]

    @property
    def text(self) -> str:
        return " ".join(line.strip() for line in self.lines if line.strip())


@dataclass(frozen=True)
class Finding:
    severity: str
    rule: str
    path: str
    line: int
    message: str


@dataclass
class ScanCounts:
    files: int = 0
    interfaces: int = 0
    cpp_sources: int = 0
    readmes: int = 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument(
        "--path",
        action="append",
        default=[],
        help="Repository-relative file or directory to audit; repeat as needed",
    )
    parser.add_argument(
        "--format", choices=("text", "json"), default="text", help="Output format"
    )
    parser.add_argument(
        "--no-fail",
        action="store_true",
        help="Return success even when objective errors are present",
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="Print counts and the highest-finding paths instead of every finding",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=20,
        help="Number of paths shown by --summary (default: 20)",
    )
    parser.add_argument(
        "--max-readme-lines",
        type=int,
        default=250,
        help="README size that becomes a review finding (default: 250)",
    )
    parser.add_argument(
        "--max-source-lines",
        type=int,
        default=1500,
        help="C++ file size that becomes a review finding (default: 1500)",
    )
    return parser.parse_args()


def is_excluded_directory(root: Path, parent: Path, name: str) -> bool:
    if name in EXCLUDED_DIRECTORY_NAMES:
        return True
    if name == "build":
        return True
    return parent == root and name.startswith("build")


def resolve_targets(root: Path, requested: list[str]) -> list[Path]:
    targets: list[Path] = []
    for raw in requested or ["."]:
        candidate = Path(raw)
        if not candidate.is_absolute():
            candidate = root / candidate
        candidate = candidate.resolve()
        try:
            candidate.relative_to(root)
        except ValueError as exc:
            raise ValueError(f"audit path is outside the repository root: {raw}") from exc
        if not candidate.exists():
            raise ValueError(f"audit path does not exist: {raw}")
        targets.append(candidate)
    return sorted(set(targets))


def discover_files(root: Path, targets: list[Path]) -> list[Path]:
    discovered: set[Path] = set()
    for target in targets:
        if target.is_file():
            candidates = [target]
        else:
            candidates = []
            for current, directory_names, file_names in os.walk(
                target, followlinks=False
            ):
                current_path = Path(current)
                directory_names[:] = sorted(
                    name
                    for name in directory_names
                    if not is_excluded_directory(root, current_path, name)
                )
                candidates.extend(current_path / name for name in sorted(file_names))
        for path in candidates:
            suffix = path.suffix.lower()
            if suffix in CPP_SUFFIXES or path.name.lower() == "readme.md":
                discovered.add(path)
    return sorted(discovered, key=lambda path: path.relative_to(root).as_posix())


def clean_line_comment(line: str) -> str:
    return re.sub(r"^\s*//[/!<]?\s?", "", line).rstrip()


def clean_block_comment(lines: list[str]) -> tuple[str, ...]:
    cleaned: list[str] = []
    for index, line in enumerate(lines):
        value = line.strip()
        if index == 0:
            value = value[2:]
        if index == len(lines) - 1 and "*/" in value:
            value = value.rsplit("*/", 1)[0]
        value = re.sub(r"^\s*\*?\s?", "", value).rstrip()
        cleaned.append(value)
    return tuple(cleaned)


def full_line_comment_blocks(lines: list[str]) -> list[CommentBlock]:
    blocks: list[CommentBlock] = []
    index = 0
    while index < len(lines):
        stripped = lines[index].lstrip()
        if stripped.startswith("//"):
            start = index
            content: list[str] = []
            while index < len(lines) and lines[index].lstrip().startswith("//"):
                content.append(clean_line_comment(lines[index]))
                index += 1
            blocks.append(CommentBlock(start + 1, index, tuple(content)))
            continue
        if stripped.startswith("/*"):
            start = index
            raw = [lines[index]]
            while "*/" not in raw[-1] and index + 1 < len(lines):
                index += 1
                raw.append(lines[index])
            index += 1
            blocks.append(CommentBlock(start + 1, index, clean_block_comment(raw)))
            continue
        index += 1
    return blocks


def leading_synopsis(lines: list[str]) -> CommentBlock | None:
    blocks_by_start = {block.start - 1: block for block in full_line_comment_blocks(lines)}
    index = 0
    while index < len(lines):
        while index < len(lines) and not lines[index].strip():
            index += 1
        block = blocks_by_start.get(index)
        if block is None:
            return None
        if LICENSE_RE.search(block.text):
            index = block.end
            continue
        return block
    return None


def synopsis_findings(path: str, lines: list[str]) -> list[Finding]:
    synopsis = leading_synopsis(lines)
    if synopsis is None:
        return [
            Finding(
                "error",
                "file-synopsis-missing",
                path,
                1,
                "module interfaces and headers require a leading what-and-why synopsis",
            )
        ]

    findings: list[Finding] = []
    content_lines = [line for line in synopsis.lines if line.strip()]
    if len(content_lines) > 3:
        findings.append(
            Finding(
                "review",
                "file-synopsis-long",
                path,
                synopsis.start,
                f"leading synopsis has {len(content_lines)} content lines; prefer one to three",
            )
        )
    words = re.findall(r"[A-Za-z0-9_]+", synopsis.text)
    if len(words) < 7 or WEAK_SYNOPSIS_RE.fullmatch(synopsis.text.strip()):
        findings.append(
            Finding(
                "review",
                "file-synopsis-weak",
                path,
                synopsis.start,
                "verify that the synopsis explains both what the file contains and why it exists",
            )
        )
    return findings


def next_declaration(lines: list[str], after_line: int) -> tuple[int, str] | None:
    index = after_line
    while index < len(lines) and not lines[index].strip():
        index += 1
    if index >= len(lines):
        return None
    start = index
    fragments: list[str] = []
    for _ in range(8):
        if index >= len(lines):
            break
        stripped = lines[index].strip()
        if stripped.startswith("//") or stripped.startswith("/*"):
            break
        fragments.append(stripped)
        if ";" in stripped or "{" in stripped:
            break
        index += 1
    declaration = " ".join(fragments)
    if declaration.startswith(("#", "import ", "module ", "export module ")):
        return None
    if TYPE_DECLARATION_RE.match(declaration) or (
        declaration.rstrip().endswith(";")
        and FUNCTION_DECLARATION_RE.match(declaration)
    ):
        return start + 1, declaration
    return None


def comment_findings(path: str, lines: list[str], interface: bool) -> list[Finding]:
    findings: list[Finding] = []
    synopsis = leading_synopsis(lines) if interface else None
    for block in full_line_comment_blocks(lines):
        if LICENSE_RE.search(block.text):
            continue
        if HISTORICAL_LANGUAGE_RE.search(block.text):
            findings.append(
                Finding(
                    "review",
                    "comment-history",
                    path,
                    block.start,
                    "rewrite chronology as a current invariant or move it to "
                    "a task, ADR, migration record, or Git",
                )
            )
        if not interface:
            continue
        if (
            synopsis is not None
            and block.start == synopsis.start
            and block.end == synopsis.end
        ):
            continue
        declaration = next_declaration(lines, block.end)
        if declaration is not None:
            line, _ = declaration
            findings.append(
                Finding(
                    "review",
                    "declaration-comment",
                    path,
                    block.start,
                    "verify this comment is mandatory to understand the "
                    f"declaration at line {line}",
                )
            )
    return findings


def readme_findings(path: str, lines: list[str], max_lines: int) -> list[Finding]:
    findings: list[Finding] = []
    if len(lines) > max_lines:
        findings.append(
            Finding(
                "review",
                "readme-large",
                path,
                1,
                f"README has {len(lines)} lines; verify it remains a concise entry point",
            )
        )

    in_fence = False
    for number, line in enumerate(lines, start=1):
        stripped = line.strip()
        if stripped.startswith(("```", "~~~")):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        heading = re.match(r"^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$", line)
        if heading and README_HISTORY_HEADING_RE.search(heading.group(1)):
            findings.append(
                Finding(
                    "error",
                    "readme-non-current-section",
                    path,
                    number,
                    "README sections must describe current state, not history, "
                    "migration journals, or future plans",
                )
            )
            continue
        if HISTORICAL_LANGUAGE_RE.search(line) or TASK_REFERENCE_RE.search(line):
            findings.append(
                Finding(
                    "review",
                    "readme-chronology",
                    path,
                    number,
                    "verify this is a concise link to an authoritative record "
                    "rather than embedded chronology",
                )
            )

    table_start = 0
    table_lines: list[str] = []
    for number, line in enumerate(lines + [""], start=1):
        if line.lstrip().startswith("|"):
            if not table_lines:
                table_start = number
            table_lines.append(line)
            continue
        if len(table_lines) >= 14:
            header = table_lines[0].lower()
            if any(word in header for word in ("api", "file", "module", "symbol", "type")):
                findings.append(
                    Finding(
                        "review",
                        "readme-manual-inventory",
                        path,
                        table_start,
                        "large hand-maintained inventory; prefer stable entry "
                        "points or generated documentation",
                    )
                )
        table_lines = []
    return findings


def scan(
    root: Path, files: list[Path], args: argparse.Namespace
) -> tuple[ScanCounts, list[Finding]]:
    counts = ScanCounts()
    findings: list[Finding] = []
    for file_path in files:
        relative = file_path.relative_to(root).as_posix()
        try:
            lines = file_path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeError) as exc:
            findings.append(
                Finding("error", "file-read-failed", relative, 1, str(exc))
            )
            continue

        counts.files += 1
        suffix = file_path.suffix.lower()
        if file_path.name.lower() == "readme.md":
            counts.readmes += 1
            findings.extend(readme_findings(relative, lines, args.max_readme_lines))
            continue

        counts.cpp_sources += 1
        interface = suffix in INTERFACE_SUFFIXES
        if interface:
            counts.interfaces += 1
            findings.extend(synopsis_findings(relative, lines))
        findings.extend(comment_findings(relative, lines, interface))
        if len(lines) > args.max_source_lines:
            findings.append(
                Finding(
                    "review",
                    "source-large",
                    relative,
                    1,
                    f"C++ file has {len(lines)} lines; inspect whether its "
                    "responsibilities remain discoverable",
                )
            )

    findings.sort(
        key=lambda finding: (
            0 if finding.severity == "error" else 1,
            finding.path,
            finding.line,
            finding.rule,
            finding.message,
        )
    )
    return counts, findings


def output_text(
    counts: ScanCounts, findings: list[Finding], summary_only: bool, top: int
) -> None:
    severity_counts = Counter(finding.severity for finding in findings)
    rule_counts = Counter(finding.rule for finding in findings)
    print(
        "[source-documentation] "
        f"files={counts.files} interfaces={counts.interfaces} "
        f"cpp={counts.cpp_sources} readmes={counts.readmes} "
        f"errors={severity_counts['error']} review={severity_counts['review']}"
    )
    if rule_counts:
        print(
            "[source-documentation] rules: "
            + ", ".join(f"{rule}={rule_counts[rule]}" for rule in sorted(rule_counts))
        )
    if summary_only:
        by_path: Counter[str] = Counter(finding.path for finding in findings)
        errors_by_path: Counter[str] = Counter(
            finding.path for finding in findings if finding.severity == "error"
        )
        for path, count in sorted(
            by_path.items(), key=lambda item: (-item[1], item[0])
        )[:top]:
            print(
                f"HOTSPOT {path} findings={count} "
                f"errors={errors_by_path[path]} review={count - errors_by_path[path]}"
            )
        return
    for finding in findings:
        print(
            f"{finding.severity.upper()} {finding.path}:{finding.line} "
            f"[{finding.rule}] {finding.message}"
        )


def output_json(counts: ScanCounts, findings: list[Finding]) -> None:
    severity_counts = Counter(finding.severity for finding in findings)
    rule_counts = Counter(finding.rule for finding in findings)
    payload = {
        "schema_version": 1,
        "summary": {
            **asdict(counts),
            "errors": severity_counts["error"],
            "review": severity_counts["review"],
            "by_rule": {rule: rule_counts[rule] for rule in sorted(rule_counts)},
        },
        "findings": [asdict(finding) for finding in findings],
    }
    print(json.dumps(payload, indent=2, sort_keys=True))


def main() -> int:
    args = parse_args()
    if args.max_readme_lines < 1 or args.max_source_lines < 1 or args.top < 1:
        print("thresholds must be positive integers", file=sys.stderr)
        return 2
    if args.summary and args.format == "json":
        print("--summary is available only with --format text", file=sys.stderr)
        return 2
    root = args.root.resolve()
    if not root.is_dir():
        print(
            f"repository root does not exist or is not a directory: {root}",
            file=sys.stderr,
        )
        return 2
    try:
        targets = resolve_targets(root, args.path)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    files = discover_files(root, targets)
    counts, findings = scan(root, files, args)
    if args.format == "json":
        output_json(counts, findings)
    else:
        output_text(counts, findings, args.summary, args.top)
    has_errors = any(finding.severity == "error" for finding in findings)
    return 0 if args.no_fail or not has_errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
