#!/usr/bin/env python3
"""Launch and validate the module-safe CI ccache pilot."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

SCHEMA_VERSION = 1
MODULE_CONTEXT_NAME = "intrinsic-ccache-module-context.txt"
MODULE_INPUT_SCHEMA_VERSION = 1

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


def _resolve_compile_input(path_text: str, cwd: Path) -> Path:
    path = Path(path_text)
    if not path.is_absolute():
        path = cwd / path
    return path.resolve()


def _read_json_object(path: Path, label: str) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"could not parse {label} {path}: {exc}") from exc
    if not isinstance(payload, dict):
        raise RuntimeError(f"{label} is not a JSON object: {path}")
    return payload


def _read_module_map_arguments(path: Path) -> list[str]:
    try:
        return shlex.split(path.read_text(encoding="utf-8"), posix=True)
    except (OSError, ValueError) as exc:
        raise RuntimeError(f"could not parse CMake module map {path}: {exc}") from exc


def _read_module_map(path: Path, cwd: Path) -> dict[str, Path]:
    mappings: dict[str, Path] = {}
    for argument in _read_module_map_arguments(path):
        if not argument.startswith("-fmodule-file="):
            continue
        mapping = argument.removeprefix("-fmodule-file=")
        logical_name, separator, pcm_path = mapping.partition("=")
        if not separator or not logical_name or not pcm_path:
            raise RuntimeError(f"invalid named-module mapping in {path}: {argument!r}")
        resolved_pcm = _resolve_compile_input(pcm_path, cwd)
        if logical_name in mappings and mappings[logical_name] != resolved_pcm:
            raise RuntimeError(
                f"conflicting named-module mapping in {path}: {logical_name}"
            )
        mappings[logical_name] = resolved_pcm
    return mappings


def _read_module_outputs(path: Path, cwd: Path) -> tuple[Path, ...]:
    outputs = {
        _resolve_compile_input(
            argument.removeprefix("-fmodule-output=").strip('"'), cwd
        )
        for argument in _read_module_map_arguments(path)
        if argument.startswith("-fmodule-output=")
    }
    return tuple(sorted(outputs, key=str))


def _read_direct_module_requirements(path: Path) -> tuple[str, ...]:
    payload = _read_json_object(path, "CMake module dependency metadata")

    rules = payload.get("rules") if isinstance(payload, dict) else None
    if not isinstance(rules, list) or not rules:
        raise RuntimeError(f"CMake module dependency metadata has no rules: {path}")

    requirements: set[str] = set()
    for rule in rules:
        if not isinstance(rule, dict):
            raise RuntimeError(f"invalid CMake module dependency rule in {path}")
        entries = rule.get("requires", [])
        if not isinstance(entries, list):
            raise RuntimeError(
                f"invalid CMake module dependency requirements in {path}"
            )
        for entry in entries:
            logical_name = (
                entry.get("logical-name") if isinstance(entry, dict) else None
            )
            if not isinstance(logical_name, str) or not logical_name:
                raise RuntimeError(
                    f"invalid CMake direct-module requirement in {path}: {entry!r}"
                )
            requirements.add(logical_name)
    return tuple(sorted(requirements))


def _provided_module(path: Path) -> tuple[str, str, str] | None:
    payload = _read_json_object(path, "CMake module dependency metadata")
    rules = payload.get("rules")
    if not isinstance(rules, list):
        raise RuntimeError(f"CMake module dependency metadata has no rules: {path}")

    providers: list[tuple[str, str, str]] = []
    for rule in rules:
        if not isinstance(rule, dict):
            raise RuntimeError(f"invalid CMake module dependency rule in {path}")
        primary_output = rule.get("primary-output")
        entries = rule.get("provides", [])
        if not isinstance(entries, list):
            raise RuntimeError(f"invalid CMake module providers in {path}")
        for entry in entries:
            logical_name = (
                entry.get("logical-name") if isinstance(entry, dict) else None
            )
            source_path = entry.get("source-path") if isinstance(entry, dict) else None
            if not all(
                isinstance(value, str) and value
                for value in (logical_name, source_path, primary_output)
            ):
                raise RuntimeError(
                    f"invalid CMake module provider in {path}: {entry!r}"
                )
            providers.append((logical_name, source_path, primary_output))
    if not providers:
        return None
    if len(providers) != 1:
        raise RuntimeError(
            f"expected at most one provided module in {path}, found {len(providers)}"
        )
    return providers[0]


def _module_compile_context(
    target_dir: Path,
    primary_output: str,
    cwd: Path,
) -> dict[str, object]:
    path = target_dir / "CXXDependInfo.json"
    payload = _read_json_object(path, "CMake C++ dependency context")
    modules = payload.get("cxx-modules")
    if not isinstance(modules, dict):
        raise RuntimeError(f"CMake C++ dependency context has no modules: {path}")

    context = modules.get(primary_output)
    if context is None:
        expected_output = _resolve_compile_input(primary_output, cwd)
        matches = [
            value
            for output, value in modules.items()
            if isinstance(output, str)
            and _resolve_compile_input(output, cwd) == expected_output
        ]
        if len(matches) == 1:
            context = matches[0]
    if not isinstance(context, dict):
        raise RuntimeError(
            f"CMake C++ dependency context has no entry for {primary_output}: {path}"
        )

    return {
        "compiler_frontend_variant": payload.get("compiler-frontend-variant"),
        "compiler_id": payload.get("compiler-id"),
        "compiler_simulate_id": payload.get("compiler-simulate-id"),
        "config": payload.get("config"),
        "language": payload.get("language"),
        "module": context,
    }


def _read_depfile_inputs(path: Path, cwd: Path) -> tuple[Path, ...]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(
            f"could not read CMake module scanner depfile {path}: {exc}"
        ) from exc
    joined = re.sub(r"\\\r?\n", " ", text)
    _, separator, dependency_text = joined.partition(":")
    if not separator:
        raise RuntimeError(f"invalid CMake module scanner depfile: {path}")
    try:
        dependencies = shlex.split(dependency_text, posix=True)
    except ValueError as exc:
        raise RuntimeError(
            f"could not parse CMake module scanner depfile {path}: {exc}"
        ) from exc
    return tuple(
        sorted(
            {
                _resolve_compile_input(dependency.replace("$$", "$"), cwd)
                for dependency in dependencies
            },
            key=str,
        )
    )


def _read_module_fingerprint(path: Path, logical_name: str) -> str:
    payload = _read_json_object(path, "module-input fingerprint")
    digest = payload.get("semantic_digest")
    if (
        payload.get("schema_version") != MODULE_INPUT_SCHEMA_VERSION
        or payload.get("logical_name") != logical_name
        or not isinstance(digest, str)
        or not re.fullmatch(r"[0-9a-f]{64}", digest)
    ):
        raise RuntimeError(
            f"module-input fingerprint is invalid for {logical_name!r}: {path}"
        )
    return digest


def _provided_module_digest(
    logical_name: str,
    source_text: str,
    primary_output: str,
    output_pcm: Path,
    dependency_path: Path,
    required_fingerprints: dict[str, Path],
    *,
    cwd: Path,
    repo_root: Path,
) -> str:
    source_path = _resolve_compile_input(source_text, cwd)
    if not source_path.is_file():
        raise RuntimeError(
            f"module provider source does not exist for {logical_name!r}: {source_path}"
        )

    try:
        dependency_roots = (repo_root.resolve(), cwd)
        dependency_files = {
            path
            for path in _read_depfile_inputs(Path(f"{dependency_path}.d"), cwd)
            if path.is_file()
            and any(_is_relative_to(path, root) for root in dependency_roots)
        }
        dependency_files.add(source_path)
        record = {
            "schema_version": MODULE_INPUT_SCHEMA_VERSION,
            "logical_name": logical_name,
            "compile_context": _module_compile_context(
                output_pcm.parent, primary_output, cwd
            ),
            "file_inputs": [
                {
                    "path": str(path),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }
                for path in sorted(dependency_files, key=str)
            ],
            "module_inputs": {
                name: _read_module_fingerprint(path, name)
                for name, path in sorted(required_fingerprints.items())
            },
        }
        encoded = json.dumps(record, sort_keys=True, separators=(",", ":")).encode(
            "utf-8"
        )
        return hashlib.sha256(encoded).hexdigest()
    except OSError as exc:
        raise RuntimeError(
            f"could not hash semantic inputs for module {logical_name!r}: {exc}"
        ) from exc


def _write_if_changed(path: Path, content: str) -> None:
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    try:
        if path.is_file() and path.read_text(encoding="utf-8") == content:
            return
        temporary.write_text(content, encoding="utf-8")
        os.replace(temporary, path)
    except OSError as exc:
        raise RuntimeError(
            f"could not write module-input fingerprint {path}: {exc}"
        ) from exc
    finally:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def module_fingerprint_inputs(
    compiler_command: list[str],
    *,
    cwd: Path | None = None,
    repo_root: Path,
) -> tuple[Path, ...]:
    working_directory = (cwd or Path.cwd()).resolve()
    response_paths = [
        _resolve_compile_input(argument[1:], working_directory)
        for argument in compiler_command
        if argument.startswith("@") and argument[1:].endswith(".modmap")
    ]
    if not response_paths:
        return ()

    fingerprint_inputs: set[Path] = set()
    for module_map_path in response_paths:
        mappings = _read_module_map(module_map_path, working_directory)
        dependency_path = module_map_path.with_suffix(".ddi")
        requirements = _read_direct_module_requirements(dependency_path)
        missing = [name for name in requirements if name not in mappings]
        if missing:
            raise RuntimeError(
                f"CMake module map {module_map_path} is missing direct requirements: "
                + ", ".join(missing)
            )
        required_fingerprints: dict[str, Path] = {}
        for name in requirements:
            pcm_path = mappings[name]
            if not pcm_path.is_file():
                raise RuntimeError(
                    f"direct module PCM input does not exist: {pcm_path}"
                )
            fingerprint_path = pcm_path.with_suffix(".ccache-inputs")
            _read_module_fingerprint(fingerprint_path, name)
            required_fingerprints[name] = fingerprint_path
            fingerprint_inputs.add(fingerprint_path)

        provided_module = _provided_module(dependency_path)
        module_outputs = _read_module_outputs(module_map_path, working_directory)
        if provided_module is None:
            if module_outputs:
                raise RuntimeError(
                    f"CMake module map has output but no provider: {module_map_path}"
                )
            continue
        if len(module_outputs) != 1:
            raise RuntimeError(
                f"expected one module output in {module_map_path}, "
                f"found {len(module_outputs)}"
            )

        logical_name, source_path, primary_output = provided_module
        output_pcm = module_outputs[0]
        semantic_digest = _provided_module_digest(
            logical_name,
            source_path,
            primary_output,
            output_pcm,
            dependency_path,
            required_fingerprints,
            cwd=working_directory,
            repo_root=repo_root,
        )
        own_fingerprint = output_pcm.with_suffix(".ccache-inputs")
        _write_if_changed(
            own_fingerprint,
            json.dumps(
                {
                    "schema_version": MODULE_INPUT_SCHEMA_VERSION,
                    "logical_name": logical_name,
                    "semantic_digest": semantic_digest,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        fingerprint_inputs.add(own_fingerprint)

    return tuple(sorted((path.resolve() for path in fingerprint_inputs), key=str))


def launch_ccache(args: argparse.Namespace) -> int:
    compiler_command = list(args.compiler_command)
    if compiler_command and compiler_command[0] == "--":
        compiler_command.pop(0)
    if not compiler_command:
        print(
            "ERROR: ccache module launcher received no compiler command",
            file=sys.stderr,
        )
        return 3

    try:
        base_extra_file = args.base_extra_file.resolve()
        if not base_extra_file.is_file():
            raise RuntimeError(
                f"base module-input fingerprint does not exist: {base_extra_file}"
            )
        module_fingerprints = module_fingerprint_inputs(
            compiler_command,
            repo_root=args.repo_root,
        )
        extra_files = (base_extra_file, *module_fingerprints)
        encoded_paths = [str(path) for path in dict.fromkeys(extra_files)]
        if any(os.pathsep in path for path in encoded_paths):
            raise RuntimeError(
                f"module-input path contains ccache list separator {os.pathsep!r}"
            )
    except RuntimeError as exc:
        print(f"ERROR: ccache module launcher: {exc}", file=sys.stderr)
        return 3

    environment = os.environ.copy()
    environment["CCACHE_NODEPEND"] = "1"
    environment["CCACHE_NODIRECT"] = "1"
    environment["CCACHE_EXTRAFILES"] = os.pathsep.join(encoded_paths)
    try:
        os.execvpe(
            str(args.ccache),
            [str(args.ccache), *compiler_command],
            environment,
        )
        return 0
    except OSError as exc:
        print(f"ERROR: could not execute ccache launcher: {exc}", file=sys.stderr)
        return 127


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
    sanitizer = cache.get("INTRINSIC_SANITIZER_IDENTITY", "")
    if sanitizer not in {"none", "asan", "ubsan", "asan-ubsan"}:
        raise RuntimeError(
            "configured CMake cache must name a valid INTRINSIC_SANITIZER_IDENTITY"
        )
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


def _validate_module_context(path: Path) -> list[str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return [f"could not read ccache global module context {path}: {exc}"]

    if (
        len(lines) != 2
        or lines[0] != "schema_version=3"
        or not re.fullmatch(r"[0-9a-f]{64}  @global-module-context", lines[1])
    ):
        return [f"ccache global module context has invalid content: {path}"]
    return []


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
            "ccache_ci.py",
            "launch",
            "--ccache=",
            "--base-extra-file=",
            "--repo-root=",
        ):
            if not any(token in line for line in launcher_lines):
                errors.append(f"generated Ninja ccache launcher is missing {token}")

    module_context = build_dir / MODULE_CONTEXT_NAME
    errors.extend(_validate_module_context(module_context))

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

    launch = subparsers.add_parser("launch")
    launch.add_argument("--ccache", type=Path, required=True)
    launch.add_argument("--base-extra-file", type=Path, required=True)
    launch.add_argument("--repo-root", type=Path, required=True)
    launch.add_argument("compiler_command", nargs=argparse.REMAINDER)
    launch.set_defaults(func=launch_ccache)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
