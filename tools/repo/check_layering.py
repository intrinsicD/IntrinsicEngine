#!/usr/bin/env python3
"""Check source-layer dependency boundaries in warning or strict mode."""

from __future__ import annotations

import argparse
import fnmatch
import re
from dataclasses import dataclass
from pathlib import Path

SOURCE_EXTS = {
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".ixx",
    ".cppm",
    ".inl",
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]')
IMPORT_RE = re.compile(r"^\s*import\s+([^;]+);")

LAYER_NAMES = {
    "core",
    "geometry",
    "assets",
    "ecs",
    "graphics_rhi",
    "graphics",
    "runtime",
    "platform",
    "app",
    "legacy",
}

ALLOWED_DEPS = {
    "core": set(),
    "geometry": {"core"},
    "assets": {"core"},
    "ecs": {"core", "geometry"},
    "graphics_rhi": {"core"},
    "graphics": {"core", "assets", "graphics_rhi", "geometry"},
    "runtime": {"core", "geometry", "assets", "ecs", "graphics_rhi", "graphics", "platform", "legacy"},
    "platform": {"core"},
    "app": {"runtime"},
    "legacy": {"legacy"},
}


@dataclass
class Violation:
    file: Path
    line: int
    source_layer: str
    target_layer: str
    ref: str
    reason: str


@dataclass
class AllowlistEntry:
    from_layer: str
    to_layer: str
    file_glob: str
    task: str
    expires: str
    reason: str


def normalize_layer_name(value: str) -> str:
    return value.strip().lower().replace("-", "_").replace("/", "_")


def detect_owner_layer(path: Path) -> str | None:
    parts = path.parts

    if "src" in parts and "legacy" in parts:
        return "legacy"

    if "src_new" in parts:
        if "Core" in parts:
            return "core"
        if "Assets" in parts:
            return "assets"
        if "ECS" in parts:
            return "ecs"
        if "Platform" in parts:
            return "platform"
        if "Runtime" in parts:
            return "runtime"
        if "App" in parts:
            return "app"
        if "Graphics" in parts and "RHI" in parts:
            return "graphics_rhi"
        if "Graphics" in parts:
            return "graphics"

    if "src" in parts:
        # Files physically under src/graphics/<sub>/ always belong to a
        # graphics layer, even when <sub> matches another top-level layer
        # name (e.g. `src/graphics/assets/` would otherwise be misclassified
        # as `assets` because "assets" appears in the path).
        if "graphics" in parts:
            if "rhi" in parts:
                return "graphics_rhi"
            return "graphics"
        if "Core" in parts or "core" in parts:
            return "core"
        if "Geometry" in parts or "geometry" in parts:
            return "geometry"
        if "Asset" in parts or "Assets" in parts or "assets" in parts:
            return "assets"
        if "ECS" in parts or "ecs" in parts:
            return "ecs"
        if "RHI" in parts or "rhi" in parts:
            return "graphics_rhi"
        if "Graphics" in parts or "graphics" in parts:
            return "graphics"
        if "Runtime" in parts or "runtime" in parts:
            return "runtime"
        if "Platform" in parts or "platform" in parts:
            return "platform"
        if "Apps" in parts or "App" in parts or "app" in parts or "EditorUI" in parts:
            return "app"

    return None


def detect_target_layer(reference: str) -> str | None:
    token = reference.strip().strip("<>").replace("\\", "/")
    lower = token.lower()

    if any(x in lower for x in ("app/", "apps/", "editorui/", "app.", "apps.")):
        return "app"
    if "runtime/" in lower or lower.startswith("runtime"):
        return "runtime"
    if "platform/" in lower or lower.startswith("platform"):
        return "platform"

    if any(x in lower for x in ("graphics/rhi", "graphics.rhi", "/rhi/", " rhi", "rhi/")):
        return "graphics_rhi"

    if "graphics/" in lower or lower.startswith("graphics"):
        return "graphics"

    if "ecs/" in lower or lower.startswith("ecs"):
        return "ecs"
    if "asset/" in lower or "assets/" in lower or lower.startswith("asset") or lower.startswith("assets"):
        return "assets"
    if "geometry/" in lower or lower.startswith("geometry"):
        return "geometry"
    if "core/" in lower or lower.startswith("core"):
        return "core"

    if "legacy/" in lower:
        return "legacy"

    return None


def extract_references(path: Path) -> list[tuple[int, str]]:
    refs: list[tuple[int, str]] = []
    try:
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except OSError:
        return refs

    for line_no, line in enumerate(lines, start=1):
        include_m = INCLUDE_RE.match(line)
        if include_m:
            refs.append((line_no, include_m.group(1).strip()))
            continue

        import_m = IMPORT_RE.match(line)
        if import_m:
            token = import_m.group(1).strip()
            if token.startswith(":"):
                continue
            refs.append((line_no, token))

    return refs


def parse_allowlist(path: Path) -> list[AllowlistEntry]:
    if not path.exists():
        return []

    entries: list[AllowlistEntry] = []
    current: dict[str, str] = {}

    for raw_line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw_line.rstrip()
        if not line or line.lstrip().startswith("#"):
            continue

        if line.lstrip().startswith("exceptions:"):
            continue

        stripped = line.lstrip()
        if stripped.startswith("- "):
            if current:
                entries.append(_entry_from_dict(current))
            current = {}
            stripped = stripped[2:]
            if ":" in stripped:
                key, value = stripped.split(":", 1)
                current[key.strip()] = value.strip().strip('"')
            continue

        if ":" in stripped:
            key, value = stripped.split(":", 1)
            current[key.strip()] = value.strip().strip('"')

    if current:
        entries.append(_entry_from_dict(current))

    return entries


def _entry_from_dict(data: dict[str, str]) -> AllowlistEntry:
    return AllowlistEntry(
        from_layer=normalize_layer_name(data.get("from", "")),
        to_layer=normalize_layer_name(data.get("to", "")),
        file_glob=data.get("file_glob", "src/legacy/**"),
        task=data.get("task", "unspecified"),
        expires=data.get("expires", "unspecified"),
        reason=data.get("reason", "unspecified"),
    )


def is_allowlisted(
    violation: Violation,
    repo_root: Path,
    allowlist: list[AllowlistEntry],
) -> AllowlistEntry | None:
    rel = violation.file.relative_to(repo_root).as_posix()
    for entry in allowlist:
        if entry.from_layer and entry.from_layer != violation.source_layer:
            continue
        if entry.to_layer and entry.to_layer != violation.target_layer:
            continue
        if not fnmatch.fnmatch(rel, entry.file_glob):
            continue
        return entry
    return None


def collect_source_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_EXTS:
            files.append(path)
    return files


def validate_allowlist_layers(entries: list[AllowlistEntry]) -> list[str]:
    errors: list[str] = []
    for entry in entries:
        if entry.from_layer and entry.from_layer not in LAYER_NAMES:
            errors.append(f"invalid from layer: {entry.from_layer}")
        if entry.to_layer and entry.to_layer not in LAYER_NAMES:
            errors.append(f"invalid to layer: {entry.to_layer}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("src"), help="Source root to scan.")
    parser.add_argument(
        "--allowlist",
        type=Path,
        default=Path("tools/repo/layering_allowlist.yaml"),
        help="YAML allowlist for temporary migration exceptions.",
    )
    parser.add_argument("--strict", action="store_true", help="Fail on violations.")
    args = parser.parse_args()

    scan_root = args.root.resolve()
    repo_root = Path.cwd().resolve()

    allowlist = parse_allowlist(args.allowlist.resolve())
    allowlist_errors = validate_allowlist_layers(allowlist)
    for err in allowlist_errors:
        print(f"[check_layering] Allowlist error: {err}")

    files = collect_source_files(scan_root)
    violations: list[Violation] = []
    ignored = 0

    for path in files:
        source_layer = detect_owner_layer(path)
        if not source_layer:
            continue

        for line_no, ref in extract_references(path):
            target_layer = detect_target_layer(ref)
            if not target_layer:
                continue
            if target_layer == source_layer:
                continue

            allowed = ALLOWED_DEPS.get(source_layer, set())
            if target_layer in allowed:
                continue

            reason = f"{source_layer} cannot depend on {target_layer}"
            violation = Violation(
                file=path,
                line=line_no,
                source_layer=source_layer,
                target_layer=target_layer,
                ref=ref,
                reason=reason,
            )

            allowlisted = is_allowlisted(violation, repo_root, allowlist)
            if allowlisted:
                ignored += 1
                continue

            violations.append(violation)

    print(f"[check_layering] Scan root: {scan_root}")
    print(f"[check_layering] Files scanned: {len(files)}")
    print(f"[check_layering] Allowlist entries: {len(allowlist)}")
    if allowlist_errors:
        print(f"[check_layering] Allowlist errors: {len(allowlist_errors)}")
    print(f"[check_layering] Allowlisted violations: {ignored}")

    if not violations and not allowlist_errors:
        print("[check_layering] No layering violations found.")
        return 0

    if violations:
        print("[check_layering] Layering violations:")
        for v in violations:
            rel = v.file.relative_to(repo_root)
            print(
                f"  - {rel}:{v.line}: {v.reason} (reference='{v.ref}')"
            )

    print("[check_layering] Action: move dependency downward, isolate seam, or add temporary allowlist entry with task/expiry.")
    if args.strict:
        print("[check_layering] STRICT MODE: failing.")
        return 1

    print("[check_layering] WARNING MODE: non-fatal.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
