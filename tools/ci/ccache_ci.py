#!/usr/bin/env python3
"""Validate the CI ccache pilot and export ccache statistics."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

SCHEMA_VERSION = 1
MODULE_DIGEST_NAME = "intrinsic-ccache-module-interfaces.txt"

SUMMARY_COUNTERS = (
    "direct_cache_hit",
    "preprocessed_cache_hit",
    "cache_miss",
    "cache_size_kibibyte",
)

# Keep this in sync with the fields marked FLAG_ERROR in ccache 4.9.1's
# src/core/Statistics.cpp. In particular, compile_failed and preprocessor_error
# are ordinary uncacheable-call results, not ccache errors.
ERROR_COUNTERS = (
    "bad_input_file",
    "bad_output_file",
    "compiler_check_failed",
    "could_not_find_compiler",
    "error_hashing_extra_file",
    "internal_error",
    "missing_cache_file",
    "modified_input_file",
)

REQUIRED_COUNTERS = SUMMARY_COUNTERS + ERROR_COUNTERS


@dataclass(frozen=True)
class CcacheSummary:
    hit_count: int
    miss_count: int
    cache_size_kib: int
    error_count: int


@dataclass(frozen=True)
class ConfiguredIdentity:
    compiler: str
    compiler_key: str
    compiler_path: str
    scan_deps_key: str
    scan_deps_path: str
    ccache_key: str
    sanitizer: str


def parse_print_stats(text: str) -> dict[str, int]:
    stats: dict[str, int] = {}
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"invalid ccache stats line {line_number}: {line!r}")
        key, value = parts
        if key in stats:
            raise ValueError(f"duplicate ccache stats counter: {key!r}")
        try:
            parsed = int(value)
        except ValueError as exc:
            raise ValueError(
                f"invalid ccache stats value for {key!r}: {value!r}"
            ) from exc
        if parsed < 0:
            raise ValueError(f"negative ccache stats value for {key!r}: {value!r}")
        stats[key] = parsed
    if not stats:
        raise ValueError("ccache stats output is empty")
    return stats


def summarize_stats(stats: dict[str, int]) -> CcacheSummary:
    missing = [counter for counter in REQUIRED_COUNTERS if counter not in stats]
    if missing:
        raise ValueError(
            "ccache stats are missing required counters: " + ", ".join(missing)
        )
    return CcacheSummary(
        hit_count=stats["direct_cache_hit"] + stats["preprocessed_cache_hit"],
        miss_count=stats["cache_miss"],
        cache_size_kib=stats["cache_size_kibibyte"],
        error_count=sum(stats[counter] for counter in ERROR_COUNTERS),
    )


def _run_ccache_config(key: str) -> str:
    completed = subprocess.run(
        ["ccache", "--get-config", key],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"ccache --get-config {key} failed: {completed.stderr.strip()}"
        )
    return completed.stdout.strip()


def _read_cmake_cache(path: Path) -> dict[str, str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(
            f"could not read configured CMake cache {path}: {exc}"
        ) from exc

    entries: dict[str, str] = {}
    for line in text.splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        if ":" not in key_and_type:
            continue
        key, _ = key_and_type.split(":", 1)
        entries[key] = value
    return entries


def _run_version(command: list[str], label: str) -> str:
    try:
        completed = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError as exc:
        raise RuntimeError(f"could not run configured {label}: {exc}") from exc
    if completed.returncode != 0:
        raise RuntimeError(
            f"configured {label} version command failed with "
            f"{completed.returncode}: {completed.stdout.strip()}"
        )
    return completed.stdout


def _extract_version(text: str, label: str) -> tuple[str, int]:
    match = re.search(r"\bversion\s+([0-9]+(?:\.[0-9]+){1,2})\b", text)
    if not match:
        raise RuntimeError(f"could not parse {label} version from: {text.strip()!r}")
    version = match.group(1)
    return version, int(version.split(".", 1)[0])


def configured_identity(build_dir: Path, expected_sanitizer: str) -> ConfiguredIdentity:
    cache = _read_cmake_cache(build_dir / "CMakeCache.txt")
    cxx_value = cache.get("CMAKE_CXX_COMPILER", "")
    scan_value = cache.get("CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS", "")
    if not cxx_value or not scan_value:
        raise RuntimeError(
            "configured CMake cache must name CMAKE_CXX_COMPILER and "
            "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS"
        )

    # Preserve the configured argv[0] spelling: clang++ commonly symlinks to
    # clang, and resolving that symlink would switch the driver back to C mode.
    cxx_path = Path(cxx_value).expanduser().absolute()
    scan_path = Path(scan_value).expanduser().absolute()
    cxx_version, cxx_major = _extract_version(
        _run_version([str(cxx_path), "--version"], "C++ compiler"),
        "C++ compiler",
    )
    scan_version, scan_major = _extract_version(
        _run_version([str(scan_path), "--version"], "clang-scan-deps"),
        "clang-scan-deps",
    )
    if cxx_major != scan_major:
        raise RuntimeError(
            f"configured compiler major {cxx_major} does not match "
            f"clang-scan-deps major {scan_major}"
        )

    ccache_version, _ = _extract_version(
        _run_version(["ccache", "--version"], "ccache"),
        "ccache",
    )
    sanitizer_enabled = cache.get("INTRINSIC_ENABLE_SANITIZERS", "").upper() in {
        "1",
        "ON",
        "TRUE",
        "YES",
        "Y",
    }
    sanitizer = "combined-project-default" if sanitizer_enabled else "none"
    if sanitizer != expected_sanitizer:
        raise RuntimeError(
            f"configured sanitizer identity is {sanitizer!r}, "
            f"expected {expected_sanitizer!r}"
        )

    return ConfiguredIdentity(
        compiler=f"clang-{cxx_major}",
        compiler_key=f"clang-{cxx_version}",
        compiler_path=str(cxx_path),
        scan_deps_key=f"clang-scan-deps-{scan_version}",
        scan_deps_path=str(scan_path),
        ccache_key=f"ccache-{ccache_version}",
        sanitizer=sanitizer,
    )


def write_configured_identity(args: argparse.Namespace) -> int:
    try:
        identity = configured_identity(args.build_dir, args.expected_sanitizer)
    except RuntimeError as exc:
        print(f"BLOCKED: {exc}", file=sys.stderr)
        return 3

    payload = asdict(identity)
    output_path = os.environ.get("GITHUB_OUTPUT")
    if output_path:
        try:
            with Path(output_path).open("a", encoding="utf-8") as output:
                for key, value in payload.items():
                    output.write(f"{key.replace('_', '-')}={value}\n")
        except OSError as exc:
            print(
                f"BLOCKED: could not publish configured identity: {exc}",
                file=sys.stderr,
            )
            return 3
    print(json.dumps(payload, sort_keys=True))
    return 0


def _is_relative_to(child: Path, parent: Path) -> bool:
    try:
        child.resolve().relative_to(parent.resolve())
    except ValueError:
        return False
    return True


def _validate_module_digest(path: Path, repo_root: Path) -> list[str]:
    errors: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return [f"could not read ccache module-interface digest {path}: {exc}"]

    if not lines or lines[0] != "schema_version=1":
        errors.append(
            f"ccache module-interface digest has invalid schema header: {path}"
        )
        return errors

    relative_paths: list[str] = []
    for line_number, line in enumerate(lines[1:], start=2):
        match = re.fullmatch(r"([0-9a-f]{64})  (.+\.cppm)", line)
        if not match:
            errors.append(
                f"invalid ccache module-interface digest line {line_number}: {line!r}"
            )
            continue
        expected_hash, relative = match.groups()
        candidate = repo_root / relative
        if Path(relative).is_absolute() or not _is_relative_to(candidate, repo_root):
            errors.append(
                f"ccache module-interface digest path escapes repository: {relative}"
            )
            continue
        try:
            observed_hash = hashlib.sha256(candidate.read_bytes()).hexdigest()
        except OSError as exc:
            errors.append(f"could not hash module interface {candidate}: {exc}")
            continue
        if observed_hash != expected_hash:
            errors.append(
                f"stale ccache module-interface digest for {relative}: "
                f"recorded {expected_hash}, observed {observed_hash}"
            )
        relative_paths.append(relative)

    if not relative_paths:
        errors.append("ccache module-interface digest contains no module interfaces")
    elif relative_paths != sorted(set(relative_paths)):
        errors.append("ccache module-interface digest paths must be sorted and unique")
    return errors


def validate_config(
    build_dir: Path,
    repo_root: Path,
    expected_cache_dir: Path,
    expected_max_size: str,
) -> list[str]:
    errors: list[str] = []
    build_ninja = build_dir / "build.ninja"
    if not build_ninja.exists():
        errors.append(f"missing generated Ninja graph: {build_ninja}")
    else:
        text = build_ninja.read_text(encoding="utf-8", errors="replace")
        launcher_lines = [
            line.strip()
            for line in text.splitlines()
            if "LAUNCHER =" in line and "ccache" in line
        ]
        if not launcher_lines:
            errors.append("generated Ninja graph does not use a ccache launcher")
        for token in (
            "CCACHE_NODEPEND=1",
            "CCACHE_NODIRECT=1",
            "CCACHE_EXTRAFILES=",
        ):
            if not any(token in line for line in launcher_lines):
                errors.append(f"generated Ninja ccache launcher is missing {token}")

    module_digest = build_dir / MODULE_DIGEST_NAME
    errors.extend(_validate_module_digest(module_digest, repo_root))

    try:
        cache_dir = Path(_run_ccache_config("cache_dir")).expanduser()
        max_size = _run_ccache_config("max_size")
        direct_mode = _run_ccache_config("direct_mode")
        depend_mode = _run_ccache_config("depend_mode")
    except RuntimeError as exc:
        errors.append(str(exc))
        return errors

    if cache_dir.resolve() != expected_cache_dir.expanduser().resolve():
        errors.append(f"ccache cache_dir is {cache_dir}, expected {expected_cache_dir}")
    if _is_relative_to(cache_dir, repo_root):
        errors.append(
            f"ccache cache_dir must not live under the repository: {cache_dir}"
        )
    if max_size != expected_max_size:
        errors.append(
            f"ccache max_size is {max_size!r}, expected {expected_max_size!r}"
        )
    if direct_mode != "false":
        errors.append(f"ccache direct_mode must be false, found {direct_mode!r}")
    if depend_mode != "false":
        errors.append(f"ccache depend_mode must be false, found {depend_mode!r}")

    return errors


def check_config(args: argparse.Namespace) -> int:
    errors = validate_config(
        args.build_dir,
        args.repo_root,
        args.expected_cache_dir,
        args.expected_max_size,
    )
    if errors:
        print("BLOCKED: ccache pilot configuration is unsafe")
        for error in errors:
            print(f" - {error}")
        return 3
    print("ccache pilot configuration passed")
    return 0


def _write_github_outputs(summary: CcacheSummary | None) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        return
    if summary is None:
        payload = "stats_available=false\n"
    else:
        payload = (
            f"hit_count={summary.hit_count}\n"
            f"miss_count={summary.miss_count}\n"
            f"cache_size_kib={summary.cache_size_kib}\n"
            f"error_count={summary.error_count}\n"
            "stats_available=true\n"
        )
    with Path(output_path).open("a", encoding="utf-8") as output:
        output.write(payload)


def _append_github_summary(summary: CcacheSummary) -> None:
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return
    with Path(summary_path).open("a", encoding="utf-8") as handle:
        handle.write("### ccache pilot statistics\n\n")
        handle.write(f"- hits: `{summary.hit_count}`\n")
        handle.write(f"- misses: `{summary.miss_count}`\n")
        handle.write(f"- cache size: `{summary.cache_size_kib} KiB`\n")
        handle.write(f"- errors: `{summary.error_count}`\n\n")


def _stats_failure(
    message: str,
    returncode: int = 2,
    output: Path | None = None,
) -> int:
    print(f"ERROR: {message}", file=sys.stderr)
    if output is not None:
        try:
            output.unlink(missing_ok=True)
        except OSError as exc:
            print(
                f"ERROR: failed to remove stale ccache stats output: {exc}",
                file=sys.stderr,
            )
    try:
        _write_github_outputs(None)
    except OSError as exc:
        print(
            f"ERROR: failed to publish unavailable ccache stats state: {exc}",
            file=sys.stderr,
        )
    return returncode if returncode > 0 else 2


def write_stats(args: argparse.Namespace) -> int:
    try:
        completed = subprocess.run(
            ["ccache", "--print-stats"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as exc:
        return _stats_failure(
            f"could not run ccache --print-stats: {exc}",
            output=args.output,
        )
    if completed.returncode != 0:
        return _stats_failure(
            f"ccache --print-stats failed: {completed.stderr.strip()}",
            completed.returncode,
            args.output,
        )

    try:
        raw_stats = parse_print_stats(completed.stdout)
        summary = summarize_stats(raw_stats)
    except ValueError as exc:
        return _stats_failure(str(exc), output=args.output)

    payload = {
        "schema_version": SCHEMA_VERSION,
        "summary": asdict(summary),
        "raw": raw_stats,
    }
    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        _append_github_summary(summary)
        _write_github_outputs(summary)
    except OSError as exc:
        return _stats_failure(
            f"failed to publish ccache stats: {exc}",
            output=args.output,
        )
    print(f"Wrote {args.output}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    check = subparsers.add_parser("check-config")
    check.add_argument("--build-dir", type=Path, required=True)
    check.add_argument("--repo-root", type=Path, required=True)
    check.add_argument("--expected-cache-dir", type=Path, required=True)
    check.add_argument("--expected-max-size", required=True)
    check.set_defaults(func=check_config)

    identity = subparsers.add_parser("configured-identity")
    identity.add_argument("--build-dir", type=Path, required=True)
    identity.add_argument("--expected-sanitizer", required=True)
    identity.set_defaults(func=write_configured_identity)

    stats = subparsers.add_parser("write-stats")
    stats.add_argument("--output", type=Path, required=True)
    stats.set_defaults(func=write_stats)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
