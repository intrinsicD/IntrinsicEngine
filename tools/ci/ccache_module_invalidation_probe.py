#!/usr/bin/env python3
"""Exercise ccache reuse across a hermetic C++23 module interface change."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import ccache_ci  # noqa: E402

SCHEMA_VERSION = 1
EXPECTED_V1_OUTPUT = "11"
EXPECTED_V2_OUTPUT = "29"
EXPECTED_COMPILE_SOURCES = ("Probe.cppm", "Probe.cpp", "main.cpp")
EXPECTED_CACHEABLE_SOURCES = ("Probe.cpp", "main.cpp")
CACHE_HIT_RESULTS = frozenset({"direct_cache_hit", "preprocessed_cache_hit"})


class ProbeError(RuntimeError):
    pass


@dataclass(frozen=True)
class ClangToolchain:
    cxx: Path
    scan_deps: Path
    major: int
    version: str


@dataclass(frozen=True)
class CcacheInvocation:
    source: str
    results: tuple[str, ...]


@dataclass(frozen=True)
class ScenarioResult:
    name: str
    source_version: str
    use_ccache: bool
    expected_output: str
    observed_output: str
    ccache_summary: dict[str, int] | None
    ccache_invocations: tuple[CcacheInvocation, ...] | None
    dependency_explanations: tuple[str, ...]


CMAKE_LISTS = """\
cmake_minimum_required(VERSION 3.28)
project(CcacheModuleInvalidationProbe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PROBE_USE_CCACHE "Compile the probe through ccache" ON)
if(PROBE_USE_CCACHE)
  find_program(CCACHE_PROGRAM ccache REQUIRED)
  set(PROBE_MODULE_FINGERPRINT "${CMAKE_CURRENT_SOURCE_DIR}/Probe.cppm")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;CCACHE_NODEPEND=1;CCACHE_NODIRECT=1;CCACHE_EXTRAFILES=${PROBE_MODULE_FINGERPRINT};${CCACHE_PROGRAM}")
endif()

add_library(probe)
target_sources(probe
  PUBLIC
    FILE_SET CXX_MODULES TYPE CXX_MODULES FILES Probe.cppm
  PRIVATE
    Probe.cpp)

add_executable(probe_app main.cpp)
target_link_libraries(probe_app PRIVATE probe)
"""

PROBE_INTERFACE_V1 = """\
export module Probe;

export struct ProbeState {
    virtual ~ProbeState();
    virtual int value() const;
    int base = 11;
};

export int probe_value();
"""

PROBE_INTERFACE_V2 = """\
export module Probe;

export struct ProbeState {
    virtual ~ProbeState();
    virtual int bias() const { return 7; }
    virtual int value() const;
    int base = 29;
    int extra = 5;
};

export int probe_value();
"""

COMMON_SOURCES = {
    "Probe.cpp": """\
module Probe;

ProbeState::~ProbeState() = default;

int ProbeState::value() const {
    return base;
}

int probe_value() {
    ProbeState state;
    return state.value();
}
""",
    "main.cpp": """\
import Probe;

#include <iostream>

int main() {
    ProbeState state;
    const int value = state.value();
    std::cout << value << "\\n";
    return 0;
}
""",
}

SOURCE_V1 = {"Probe.cppm": PROBE_INTERFACE_V1, **COMMON_SOURCES}
SOURCE_V2 = {"Probe.cppm": PROBE_INTERFACE_V2, **COMMON_SOURCES}


def _run(
    command: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as exc:
        rendered = " ".join(command)
        raise ProbeError(f"could not run command: {rendered}: {exc}") from exc
    if completed.returncode != 0:
        rendered = " ".join(command)
        raise ProbeError(
            f"command failed with exit {completed.returncode}: {rendered}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    return completed


def _clang_major_from_version(text: str) -> int | None:
    match = re.search(r"version\s+([0-9]+)(?:\.[0-9]+)*", text)
    if not match:
        return None
    return int(match.group(1))


def _scan_deps_for(cxx: Path, major: int) -> Path | None:
    suffix_match = re.fullmatch(r"clang\+\+-(\d+)", cxx.name)
    names = [f"clang-scan-deps-{major}", "clang-scan-deps"]
    if suffix_match:
        names = [f"clang-scan-deps-{suffix_match.group(1)}"]
    for name in names:
        found = shutil.which(name)
        if found:
            return Path(found)
    return None


def clang_toolchain_from_paths(
    cxx: Path,
    scan_deps: Path,
    min_major: int = 20,
) -> ClangToolchain:
    # Preserve clang++ argv[0] instead of resolving its symlink to clang, which
    # would switch the driver back to C mode.
    cxx = cxx.expanduser().absolute()
    scan_deps = scan_deps.expanduser().absolute()
    cxx_completed = _run([str(cxx), "--version"])
    scan_completed = _run([str(scan_deps), "--version"])
    cxx_version_text = f"{cxx_completed.stdout}\n{cxx_completed.stderr}".strip()
    scan_version_text = f"{scan_completed.stdout}\n{scan_completed.stderr}".strip()
    cxx_version = cxx_version_text.splitlines()[0]
    cxx_major = _clang_major_from_version(cxx_version_text)
    scan_major = _clang_major_from_version(scan_version_text)
    if cxx_major is None or cxx_major < min_major:
        raise ProbeError(
            f"configured C++ compiler must be Clang {min_major}+, "
            f"found {cxx_version_text!r}"
        )
    if scan_major is None:
        raise ProbeError(
            f"could not parse configured clang-scan-deps version: {scan_version_text!r}"
        )
    if cxx_major != scan_major:
        raise ProbeError(
            f"configured compiler major {cxx_major} does not match "
            f"clang-scan-deps major {scan_major}"
        )
    return ClangToolchain(
        cxx=cxx,
        scan_deps=scan_deps,
        major=cxx_major,
        version=cxx_version,
    )


def find_clang_toolchain(min_major: int = 20) -> ClangToolchain:
    candidates: set[Path] = set()
    path_dirs = [
        Path(entry) for entry in os.environ.get("PATH", "").split(os.pathsep) if entry
    ]
    for directory in path_dirs:
        try:
            candidates.update(directory.glob("clang++"))
            candidates.update(directory.glob("clang++-[0-9]*"))
        except OSError:
            continue

    complete: list[ClangToolchain] = []
    for cxx in candidates:
        if not cxx.is_file():
            continue
        version = _run([str(cxx), "--version"]).stdout.splitlines()[0]
        major = _clang_major_from_version(version)
        if major is None or major < min_major:
            continue
        scan_deps = _scan_deps_for(cxx, major)
        if scan_deps is None:
            continue
        try:
            complete.append(
                clang_toolchain_from_paths(cxx, scan_deps, min_major=min_major)
            )
        except ProbeError:
            continue

    if not complete:
        raise ProbeError(
            f"no complete Clang {min_major}+ and clang-scan-deps toolchain found"
        )
    return sorted(complete, key=lambda item: (item.major, str(item.cxx)))[-1]


def write_sources(source_dir: Path, version: str) -> None:
    source_dir.mkdir(parents=True, exist_ok=True)
    (source_dir / "CMakeLists.txt").write_text(CMAKE_LISTS, encoding="utf-8")
    source_map = SOURCE_V1 if version == "v1" else SOURCE_V2
    for relative, text in source_map.items():
        (source_dir / relative).write_text(text, encoding="utf-8")


def write_interface(source_dir: Path, version: str) -> None:
    interface = PROBE_INTERFACE_V1 if version == "v1" else PROBE_INTERFACE_V2
    (source_dir / "Probe.cppm").write_text(interface, encoding="utf-8")


def _source_fingerprints(source_dir: Path) -> dict[str, dict[str, int | str]]:
    fingerprints: dict[str, dict[str, int | str]] = {}
    for name in EXPECTED_COMPILE_SOURCES:
        path = source_dir / name
        fingerprints[name] = {
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "mtime_ns": path.stat().st_mtime_ns,
        }
    return fingerprints


def _configure(
    source_dir: Path,
    build_dir: Path,
    toolchain: ClangToolchain,
    *,
    use_ccache: bool,
    env: dict[str, str],
) -> None:
    build_dir.mkdir(parents=True, exist_ok=True)
    _run(
        [
            "cmake",
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            f"-DCMAKE_CXX_COMPILER={toolchain.cxx}",
            f"-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS={toolchain.scan_deps}",
            f"-DPROBE_USE_CCACHE={'ON' if use_ccache else 'OFF'}",
        ],
        env=env,
    )


def _build_run(
    build_dir: Path,
    *,
    env: dict[str, str],
) -> tuple[str, tuple[str, ...]]:
    completed = _run(
        ["ninja", "-C", str(build_dir), "-d", "explain", "-v", "probe_app"],
        env=env,
    )
    build_output = f"{completed.stdout}\n{completed.stderr}"
    explanations = tuple(
        line.strip() for line in build_output.splitlines() if "ninja explain:" in line
    )
    observed = _run([str(build_dir / "probe_app")], env=env).stdout.strip()
    return observed, explanations


def _ccache_summary(env: dict[str, str]) -> dict[str, int]:
    completed = _run(["ccache", "--print-stats"], env=env)
    stats = ccache_ci.parse_print_stats(completed.stdout)
    return asdict(ccache_ci.summarize_stats(stats))


def _parse_ccache_stats_log(path: Path) -> tuple[CcacheInvocation, ...]:
    if not path.exists():
        raise ProbeError(f"ccache did not write its per-source stats log: {path}")

    invocations: list[CcacheInvocation] = []
    source: str | None = None
    results: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            if source is not None:
                invocations.append(CcacheInvocation(source, tuple(results)))
            source = Path(line[2:]).name
            results = []
        elif line and source is not None:
            results.append(line)
    if source is not None:
        invocations.append(CcacheInvocation(source, tuple(results)))
    return tuple(invocations)


def _zero_ccache(env: dict[str, str]) -> None:
    _run(["ccache", "--zero-stats"], env=env)


def _configure_ccache(
    env: dict[str, str],
    cache_dir: Path,
    max_size: str,
    module_fingerprint: Path,
) -> None:
    cache_dir.mkdir(parents=True, exist_ok=True)
    _run(["ccache", f"--set-config=cache_dir={cache_dir}"], env=env)
    _run(["ccache", f"--set-config=max_size={max_size}"], env=env)
    _run(["ccache", "--set-config=direct_mode=false"], env=env)
    _run(["ccache", "--set-config=depend_mode=false"], env=env)
    _run(
        ["ccache", f"--set-config=extra_files_to_hash={module_fingerprint}"],
        env=env,
    )


def run_scenario(
    *,
    name: str,
    build_dir: Path,
    stats_dir: Path,
    source_version: str,
    expected_output: str,
    use_ccache: bool,
    env: dict[str, str],
) -> ScenarioResult:
    stats_log = stats_dir / f"{name}.log"
    scenario_env = env.copy()
    if use_ccache:
        stats_dir.mkdir(parents=True, exist_ok=True)
        stats_log.unlink(missing_ok=True)
        scenario_env["CCACHE_STATSLOG"] = str(stats_log)
        _zero_ccache(scenario_env)
    observed, explanations = _build_run(build_dir, env=scenario_env)
    if observed != expected_output:
        raise ProbeError(f"{name} produced {observed!r}, expected {expected_output!r}")
    summary = _ccache_summary(scenario_env) if use_ccache else None
    invocations = _parse_ccache_stats_log(stats_log) if use_ccache else None
    if summary is not None and summary["error_count"] != 0:
        raise ProbeError(f"{name} reported ccache errors: {summary}")
    return ScenarioResult(
        name=name,
        source_version=source_version,
        use_ccache=use_ccache,
        expected_output=expected_output,
        observed_output=observed,
        ccache_summary=summary,
        ccache_invocations=invocations,
        dependency_explanations=explanations,
    )


def _single_invocation_results(
    scenario: ScenarioResult,
    source: str,
    errors: list[str],
) -> tuple[str, ...] | None:
    invocations = scenario.ccache_invocations or ()
    matches = [item.results for item in invocations if item.source == source]
    if len(matches) != 1:
        errors.append(
            f"{scenario.name} expected one ccache invocation for {source}, found {len(matches)}"
        )
        return None
    return matches[0]


def _cache_evidence_errors(
    cold: ScenarioResult,
    warm: ScenarioResult,
    changed: ScenarioResult,
) -> tuple[list[str], str]:
    errors: list[str] = []
    interface_mode = "unknown"

    for scenario in (cold, warm, changed):
        actual_sources = [
            invocation.source for invocation in scenario.ccache_invocations or ()
        ]
        if sorted(actual_sources) != sorted(EXPECTED_COMPILE_SOURCES):
            errors.append(
                f"{scenario.name} ccache sources were {actual_sources}, "
                f"expected {list(EXPECTED_COMPILE_SOURCES)}"
            )

    for source in EXPECTED_CACHEABLE_SOURCES:
        cold_results = _single_invocation_results(cold, source, errors)
        if cold_results is not None and "cache_miss" not in cold_results:
            errors.append(
                f"{cold.name} {source} was not stored as a cold cache miss: {cold_results}"
            )

        warm_results = _single_invocation_results(warm, source, errors)
        if warm_results is not None and not CACHE_HIT_RESULTS.intersection(
            warm_results
        ):
            errors.append(
                f"{warm.name} {source} did not produce an unchanged-source cache hit: "
                f"{warm_results}"
            )

        changed_results = _single_invocation_results(changed, source, errors)
        if changed_results is not None and "cache_miss" not in changed_results:
            errors.append(
                f"{changed.name} {source} was not invalidated by the module fingerprint: "
                f"{changed_results}"
            )

    cold_interface = _single_invocation_results(cold, "Probe.cppm", errors)
    warm_interface = _single_invocation_results(warm, "Probe.cppm", errors)
    changed_interface = _single_invocation_results(changed, "Probe.cppm", errors)
    if cold_interface is not None:
        if "unsupported_source_language" in cold_interface:
            interface_mode = "compiler-pass-through"
            for scenario, results in (
                (warm, warm_interface),
                (changed, changed_interface),
            ):
                if results is not None and "unsupported_source_language" not in results:
                    errors.append(
                        f"{scenario.name} changed Probe.cppm cache support unexpectedly: {results}"
                    )
        elif "cache_miss" in cold_interface:
            interface_mode = "cached"
            if warm_interface is not None and not CACHE_HIT_RESULTS.intersection(
                warm_interface
            ):
                errors.append(
                    f"{warm.name} Probe.cppm did not hit after a cached cold compile: {warm_interface}"
                )
            if changed_interface is not None and "cache_miss" not in changed_interface:
                errors.append(
                    f"{changed.name} Probe.cppm did not miss after its interface changed: "
                    f"{changed_interface}"
                )
        else:
            errors.append(
                f"{cold.name} ccache does not support/cache the Probe.cppm compilation "
                f"in a recognized fail-closed mode: {cold_interface}"
            )

    expected_cacheable_count = len(EXPECTED_CACHEABLE_SOURCES) + (
        1 if interface_mode == "cached" else 0
    )
    cold_summary = cold.ccache_summary or {}
    warm_summary = warm.ccache_summary or {}
    changed_summary = changed.ccache_summary or {}
    if cold_summary.get("miss_count") != expected_cacheable_count:
        errors.append(
            f"{cold.name} expected {expected_cacheable_count} misses, found "
            f"{cold_summary.get('miss_count', 0)}"
        )
    if warm_summary.get("hit_count") != expected_cacheable_count:
        errors.append(
            f"{warm.name} expected {expected_cacheable_count} hits, found "
            f"{warm_summary.get('hit_count', 0)}"
        )
    if warm_summary.get("hit_count", 0) == 0:
        errors.append(f"{warm.name} produced zero cache hits")
    if changed_summary.get("miss_count") != expected_cacheable_count:
        errors.append(
            f"{changed.name} expected {expected_cacheable_count} invalidation misses, found "
            f"{changed_summary.get('miss_count', 0)}"
        )
    return errors, interface_mode


def run_probe(args: argparse.Namespace) -> dict[str, object]:
    for tool in ("cmake", "ninja", "ccache"):
        if shutil.which(tool) is None:
            raise ProbeError(f"required tool not found in PATH: {tool}")

    ccache_completed = _run(["ccache", "--version"])
    ccache_version = (
        f"{ccache_completed.stdout}\n{ccache_completed.stderr}".strip().splitlines()[0]
    )

    if (args.cxx is None) != (args.scan_deps is None):
        raise ProbeError("--cxx and --scan-deps must be provided together")
    toolchain = (
        clang_toolchain_from_paths(args.cxx, args.scan_deps)
        if args.cxx is not None and args.scan_deps is not None
        else find_clang_toolchain()
    )
    if args.work_dir is None:
        temp_context = tempfile.TemporaryDirectory(
            prefix="intrinsic-ccache-module-probe-"
        )
        work_dir = Path(temp_context.name)
    else:
        temp_context = None
        work_dir = args.work_dir
        if work_dir.exists():
            raise ProbeError(f"--work-dir must not already exist: {work_dir}")
        work_dir.mkdir(parents=True)

    try:
        source_dir = work_dir / "src"
        cache_dir = work_dir / "ccache"
        stats_dir = work_dir / "ccache-stats"
        cached_build_dir = work_dir / "build-cached"
        clean_build_dir = work_dir / "build-clean-v2"
        config_path = work_dir / "ccache.conf"
        write_sources(source_dir, "v1")
        v1_fingerprints = _source_fingerprints(source_dir)

        env = os.environ.copy()
        for key in ("CCACHE_DEPEND", "CCACHE_DIRECT", "CCACHE_STATSLOG"):
            env.pop(key, None)
        env["CCACHE_CONFIGPATH"] = str(config_path)
        env["CCACHE_DIR"] = str(cache_dir)
        env["CCACHE_MAXSIZE"] = args.ccache_max_size
        env["CCACHE_NODEPEND"] = "1"
        env["CCACHE_NODIRECT"] = "1"
        env["CCACHE_EXTRAFILES"] = str(source_dir / "Probe.cppm")
        _configure_ccache(
            env,
            cache_dir,
            args.ccache_max_size,
            source_dir / "Probe.cppm",
        )
        _configure(
            source_dir,
            cached_build_dir,
            toolchain,
            use_ccache=True,
            env=env,
        )

        cold_v1 = run_scenario(
            name="empty-cache-v1",
            build_dir=cached_build_dir,
            stats_dir=stats_dir,
            source_version="v1",
            expected_output=EXPECTED_V1_OUTPUT,
            use_ccache=True,
            env=env,
        )
        _run(["ninja", "-C", str(cached_build_dir), "clean"], env=env)
        warm_v1 = run_scenario(
            name="restored-cache-unchanged-v1",
            build_dir=cached_build_dir,
            stats_dir=stats_dir,
            source_version="v1",
            expected_output=EXPECTED_V1_OUTPUT,
            use_ccache=True,
            env=env,
        )

        after_warm_fingerprints = _source_fingerprints(source_dir)
        if after_warm_fingerprints != v1_fingerprints:
            raise ProbeError("the unchanged rebuild modified probe source inputs")

        interface_path = source_dir / "Probe.cppm"
        previous_interface_mtime = interface_path.stat().st_mtime_ns
        write_interface(source_dir, "v2")
        updated_interface_stat = interface_path.stat()
        if updated_interface_stat.st_mtime_ns <= previous_interface_mtime:
            # Avoid a sleep while still making Ninja invalidation deterministic
            # on filesystems with coarse timestamp resolution.
            os.utime(
                interface_path,
                ns=(
                    updated_interface_stat.st_atime_ns,
                    previous_interface_mtime + 1_000_000_000,
                ),
            )
        v2_fingerprints = _source_fingerprints(source_dir)
        for source in EXPECTED_CACHEABLE_SOURCES:
            if v2_fingerprints[source] != v1_fingerprints[source]:
                raise ProbeError(
                    f"interface-change scenario modified stable source {source}: "
                    f"v1={v1_fingerprints[source]} v2={v2_fingerprints[source]}"
                )
        if (
            v2_fingerprints["Probe.cppm"]["sha256"]
            == v1_fingerprints["Probe.cppm"]["sha256"]
        ):
            raise ProbeError("interface-change scenario did not change Probe.cppm")

        cached_v2_scenario = run_scenario(
            name="restored-cache-interface-change-v2",
            build_dir=cached_build_dir,
            stats_dir=stats_dir,
            source_version="v2",
            expected_output=EXPECTED_V2_OUTPUT,
            use_ccache=True,
            env=env,
        )
        _configure(
            source_dir,
            clean_build_dir,
            toolchain,
            use_ccache=False,
            env=env,
        )
        clean_v2_scenario = run_scenario(
            name="clean-no-ccache-interface-change-v2",
            build_dir=clean_build_dir,
            stats_dir=stats_dir,
            source_version="v2",
            expected_output=EXPECTED_V2_OUTPUT,
            use_ccache=False,
            env=env,
        )
        scenarios = [cold_v1, warm_v1, cached_v2_scenario, clean_v2_scenario]

        evidence_errors, interface_cache_mode = _cache_evidence_errors(
            cold_v1,
            warm_v1,
            cached_v2_scenario,
        )
        module_dependency_dirty = any(
            "Probe.pcm is dirty" in line
            for line in cached_v2_scenario.dependency_explanations
        )
        consumer_dependency_dirty = any(
            "CMakeFiles/probe_app.dir/main.cpp.o is dirty" in line
            for line in cached_v2_scenario.dependency_explanations
        )
        consumer_recompiled = any(
            invocation.source == "main.cpp"
            for invocation in cached_v2_scenario.ccache_invocations or ()
        )
        if not module_dependency_dirty:
            evidence_errors.append(
                "interface-change build did not explain Probe.pcm as dirty"
            )
        if not consumer_dependency_dirty:
            evidence_errors.append(
                "interface-change build did not explain the unchanged main.cpp object as dirty"
            )
        if not consumer_recompiled:
            evidence_errors.append(
                "interface-change build did not recompile the unchanged main.cpp consumer"
            )
        if evidence_errors:
            formatted = "\n - ".join(evidence_errors)
            raise ProbeError(
                "ccache module invalidation evidence failed closed:\n - " + formatted
            )

        cached_v2 = scenarios[2].observed_output
        clean_v2 = scenarios[3].observed_output
        parity_matched = cached_v2 == clean_v2
        if not parity_matched:
            raise ProbeError(
                f"cached interface-change output {cached_v2!r} differs from clean output {clean_v2!r}"
            )

        return {
            "schema_version": SCHEMA_VERSION,
            "status": "passed",
            "work_dir": str(work_dir),
            "cache_dir": str(cache_dir),
            "ccache_config_path": str(config_path),
            "ccache_max_size": args.ccache_max_size,
            "ccache_version": ccache_version,
            "cache_mode": {
                "direct_mode": False,
                "depend_mode": False,
                "extra_files_to_hash": [str(source_dir / "Probe.cppm")],
                "interface_compilation": interface_cache_mode,
            },
            "toolchain": {
                "cxx": str(toolchain.cxx),
                "clang_scan_deps": str(toolchain.scan_deps),
                "major": toolchain.major,
                "version": toolchain.version,
            },
            "scenarios": [asdict(scenario) for scenario in scenarios],
            "source_invariance": {
                "v1": v1_fingerprints,
                "v2": v2_fingerprints,
                "interface_changed": True,
                "implementation_unchanged": True,
                "consumer_unchanged": True,
            },
            "dependency_invalidation": {
                "module_dependency_dirty": module_dependency_dirty,
                "consumer_dependency_dirty": consumer_dependency_dirty,
                "consumer_recompiled": consumer_recompiled,
            },
            "parity": {
                "cached_interface_change_output": cached_v2,
                "clean_no_ccache_output": clean_v2,
                "matched": parity_matched,
            },
        }
    finally:
        if temp_context is not None:
            temp_context.cleanup()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--cxx", type=Path)
    parser.add_argument("--scan-deps", type=Path)
    parser.add_argument("--ccache-max-size", default="128M")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        payload = run_probe(args)
    except ProbeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote {args.output}")
    print(
        "module invalidation probe passed: "
        f"cached v2={payload['parity']['cached_interface_change_output']} "
        f"clean v2={payload['parity']['clean_no_ccache_output']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
