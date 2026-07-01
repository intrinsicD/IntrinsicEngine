#!/usr/bin/env python3
"""Check source-layer dependency boundaries in warning or strict mode.

The checker classifies references that cross layer boundaries by inspecting:

- C-style ``#include`` directives in source files.
- ``import <module-name>;`` directives in C++23 module sources, including
  promoted ``Extrinsic.<Layer>.*`` module prefixes that name a layer through
  the module identifier rather than a filesystem path.
- ``target_link_libraries(<target> ... <dep>)`` calls in ``CMakeLists.txt``
  files, where promoted CMake target names (``ExtrinsicCore``,
  ``ExtrinsicPlatform``, ``ExtrinsicGraphics``, ...) identify a layer.

Each violation is reported with the source file path, line number, source
layer, target layer, the offending reference, and the kind of edge
(``import`` for an ``#include``/``import`` and ``cmake_link`` for a CMake
link edge). Resolve by moving the dependency downward, introducing a
backend-specific seam, or adding a tracked temporary allowlist entry with a
``task`` and ``expires`` field.
"""

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

CMAKE_FILE_NAMES = {"CMakeLists.txt"}
CMAKE_FILE_EXTS = {".cmake"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]')
IMPORT_RE = re.compile(r"^\s*(?:export\s+)?import\s+([^;]+);")
CMAKE_TLL_RE = re.compile(r"target_link_libraries\s*\(", re.IGNORECASE)
CMAKE_COMMENT_RE = re.compile(r"#.*$")
CMAKE_KEYWORDS = {
    "PUBLIC",
    "PRIVATE",
    "INTERFACE",
    "LINK_PUBLIC",
    "LINK_PRIVATE",
    "LINK_INTERFACE_LIBRARIES",
}

LAYER_NAMES = {
    "core",
    "geometry",
    "assets",
    "ecs",
    "physics",
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
    "physics": {"core", "geometry"},
    "graphics_rhi": {"core"},
    "graphics": {"core", "assets", "graphics_rhi", "geometry"},
    "runtime": {"core", "geometry", "assets", "ecs", "physics", "graphics_rhi", "graphics", "platform", "legacy"},
    "platform": {"core"},
    "app": {"runtime"},
    "legacy": {"legacy"},
}

# Promoted C++23 module-prefix to owning layer. Longest prefixes first so the
# RHI prefix is preferred over the generic Graphics prefix.
MODULE_PREFIX_LAYERS: tuple[tuple[str, str], ...] = (
    ("Extrinsic.RHI.", "graphics_rhi"),
    ("Extrinsic.Backends.Vulkan", "graphics"),
    ("Extrinsic.Graphics.RenderGraph.", "graphics"),
    ("Extrinsic.Graphics.Assets.", "graphics"),
    ("Extrinsic.Graphics.", "graphics"),
    ("Extrinsic.Asset.", "assets"),
    ("Extrinsic.Assets.", "assets"),
    ("Extrinsic.ECS.", "ecs"),
    ("Extrinsic.Physics.", "physics"),
    ("Extrinsic.Geometry.", "geometry"),
    ("Extrinsic.Core.", "core"),
    ("Extrinsic.Platform.", "platform"),
    ("Extrinsic.Runtime.", "runtime"),
    ("Extrinsic.Sandbox", "app"),
    ("Extrinsic.App.", "app"),
    ("Extrinsic.EditorUI", "app"),
    ("Geometry.HalfedgeMesh", "geometry"),
    ("Geometry.PointCloud", "geometry"),
    ("Geometry.Graph", "geometry"),
    ("Geometry.", "geometry"),
    ("Asset.Registry", "assets"),
)

# Promoted CMake target name to owning layer. Longest names first to avoid
# prefix collisions (e.g. ``ExtrinsicGraphicsRenderGraph`` vs
# ``ExtrinsicGraphics``).
CMAKE_TARGET_LAYERS: tuple[tuple[str, str], ...] = (
    ("ExtrinsicGraphicsRenderGraph", "graphics"),
    ("ExtrinsicGraphicsAssets", "graphics"),
    ("ExtrinsicBackendsVulkan", "graphics"),
    ("ExtrinsicGraphics", "graphics"),
    ("ExtrinsicRHI", "graphics_rhi"),
    ("ExtrinsicAssets", "assets"),
    ("ExtrinsicECS", "ecs"),
    ("ExtrinsicPhysics", "physics"),
    ("ExtrinsicGeometry", "geometry"),
    ("IntrinsicGeometry", "geometry"),
    ("ExtrinsicPlatform", "platform"),
    ("ExtrinsicRuntime", "runtime"),
    ("ExtrinsicCore", "core"),
    ("ExtrinsicSandbox", "app"),
    ("ExtrinsicApp", "app"),
)


@dataclass
class Violation:
    file: Path
    line: int
    source_layer: str
    target_layer: str
    ref: str
    reason: str
    kind: str  # "import" or "cmake_link"


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
        if "Physics" in parts or "physics" in parts:
            return "physics"
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

    # C++23 module identifiers: ``Extrinsic.<Layer>.<...>`` and the small
    # set of legacy-style ``Geometry.*`` / ``Asset.Registry`` modules.
    for prefix, layer in MODULE_PREFIX_LAYERS:
        if token == prefix.rstrip(".") or token.startswith(prefix):
            return layer

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
    if "physics/" in lower or lower.startswith("physics"):
        return "physics"
    if "asset/" in lower or "assets/" in lower or lower.startswith("asset") or lower.startswith("assets"):
        return "assets"
    if "geometry/" in lower or lower.startswith("geometry"):
        return "geometry"
    if "core/" in lower or lower.startswith("core"):
        return "core"

    if "legacy/" in lower:
        return "legacy"

    return None


def detect_cmake_target_layer(name: str) -> str | None:
    """Map a promoted CMake target name to its owning layer.

    Returns ``None`` for unknown names (third-party libraries, generator
    expressions, language keywords); the caller skips these.
    """

    cleaned = name.strip()
    if not cleaned:
        return None
    if cleaned in CMAKE_KEYWORDS:
        return None
    if cleaned.startswith("$<") or cleaned.startswith("${"):
        return None
    for target_name, layer in CMAKE_TARGET_LAYERS:
        if cleaned == target_name:
            return layer
    return None


def _strip_cmake_comment(line: str) -> str:
    return CMAKE_COMMENT_RE.sub("", line)


def extract_cmake_link_references(path: Path) -> list[tuple[int, str]]:
    """Yield ``(line_no, token)`` pairs for every ``target_link_libraries``
    argument across one or more multi-line calls in ``path``.

    The first argument (the target being linked) is skipped — it is the
    owning module, not a dependency. Keywords (``PUBLIC``/``PRIVATE``/...)
    are skipped. Tokens beginning with generator expressions or variable
    references (``$<``, ``${``) are skipped because the checker cannot
    resolve them statically; bare promoted target names are mapped through
    :func:`detect_cmake_target_layer`.
    """

    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []

    refs: list[tuple[int, str]] = []
    lines = text.splitlines()

    in_call = False
    saw_target_arg = False
    paren_depth = 0

    for line_no, raw in enumerate(lines, start=1):
        line = _strip_cmake_comment(raw)
        idx = 0
        while idx < len(line):
            ch = line[idx]
            if not in_call:
                match = CMAKE_TLL_RE.search(line, idx)
                if not match:
                    break
                in_call = True
                saw_target_arg = False
                paren_depth = 1
                idx = match.end()
                # The remainder of this line is parsed as arguments.
                remainder = line[idx:]
                tokens, paren_depth, exhausted = _consume_cmake_args(remainder, paren_depth)
                for token in tokens:
                    if not saw_target_arg:
                        saw_target_arg = True
                        continue
                    refs.append((line_no, token))
                if paren_depth == 0:
                    in_call = False
                idx = len(line) if exhausted else len(line)
            else:
                tokens, paren_depth, _exhausted = _consume_cmake_args(line[idx:], paren_depth)
                for token in tokens:
                    if not saw_target_arg:
                        saw_target_arg = True
                        continue
                    refs.append((line_no, token))
                if paren_depth == 0:
                    in_call = False
                idx = len(line)

    return refs


def _consume_cmake_args(text: str, paren_depth: int) -> tuple[list[str], int, bool]:
    """Parse CMake arguments until the matching close paren or end of line.

    Returns ``(tokens, paren_depth, exhausted)`` where ``exhausted`` is True
    when the parser reached the end of the input slice (i.e., the call may
    continue on the next physical line).
    """

    tokens: list[str] = []
    buf: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "(":
            paren_depth += 1
            buf.append(ch)
        elif ch == ")":
            paren_depth -= 1
            if paren_depth == 0:
                if buf:
                    tokens.append("".join(buf).strip())
                    buf = []
                return tokens, paren_depth, False
            buf.append(ch)
        elif ch.isspace():
            if buf:
                tokens.append("".join(buf).strip())
                buf = []
        elif ch == '"':
            # Quoted string — consume until closing quote.
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                j += 1
            tokens.append(text[i + 1 : j])
            i = j  # skip closing quote
        else:
            buf.append(ch)
        i += 1

    if buf:
        tokens.append("".join(buf).strip())
    return tokens, paren_depth, True


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


def _is_excluded(path: Path, root: Path, exclude_patterns: list[str]) -> bool:
    if not exclude_patterns:
        return False
    try:
        rel = path.relative_to(root).as_posix()
    except ValueError:
        rel = path.as_posix()
    for pattern in exclude_patterns:
        if fnmatch.fnmatch(rel, pattern):
            return True
        # Treat plain substrings as path-segment globs for convenience: a
        # bare ``negative`` excludes anything containing ``/negative/`` or
        # ending with ``/negative``.
        if "/" not in pattern and "*" not in pattern and "?" not in pattern:
            segments = rel.split("/")
            if pattern in segments:
                return True
    return False


def collect_source_files(root: Path, exclude_patterns: list[str]) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if _is_excluded(path, root, exclude_patterns):
            continue
        suffix = path.suffix.lower()
        name = path.name
        if suffix in SOURCE_EXTS:
            files.append(path)
        elif name in CMAKE_FILE_NAMES or suffix in CMAKE_FILE_EXTS:
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
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="PATTERN",
        help=(
            "Skip files whose path (relative to --root) matches PATTERN. "
            "Patterns are matched as fnmatch globs against the full relative "
            "path; bare names match any path segment. Repeatable."
        ),
    )
    args = parser.parse_args()

    scan_root = args.root.resolve()
    repo_root = Path.cwd().resolve()

    allowlist = parse_allowlist(args.allowlist.resolve())
    allowlist_errors = validate_allowlist_layers(allowlist)
    for err in allowlist_errors:
        print(f"[check_layering] Allowlist error: {err}")

    files = collect_source_files(scan_root, args.exclude)
    violations: list[Violation] = []
    ignored = 0
    import_ref_count = 0
    cmake_ref_count = 0

    for path in files:
        source_layer = detect_owner_layer(path)
        if not source_layer:
            continue

        is_cmake = path.name in CMAKE_FILE_NAMES or path.suffix.lower() in CMAKE_FILE_EXTS

        if is_cmake:
            for line_no, ref in extract_cmake_link_references(path):
                cmake_ref_count += 1
                target_layer = detect_cmake_target_layer(ref)
                if not target_layer:
                    continue
                if target_layer == source_layer:
                    continue
                allowed = ALLOWED_DEPS.get(source_layer, set())
                if target_layer in allowed:
                    continue
                reason = (
                    f"{source_layer} cannot depend on {target_layer} "
                    f"via CMake target_link_libraries({ref})"
                )
                violation = Violation(
                    file=path,
                    line=line_no,
                    source_layer=source_layer,
                    target_layer=target_layer,
                    ref=ref,
                    reason=reason,
                    kind="cmake_link",
                )
                allowlisted = is_allowlisted(violation, repo_root, allowlist)
                if allowlisted:
                    ignored += 1
                    continue
                violations.append(violation)
            continue

        for line_no, ref in extract_references(path):
            import_ref_count += 1
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
                kind="import",
            )

            allowlisted = is_allowlisted(violation, repo_root, allowlist)
            if allowlisted:
                ignored += 1
                continue

            violations.append(violation)

    print(f"[check_layering] Scan root: {scan_root}")
    print(f"[check_layering] Files scanned: {len(files)}")
    print(
        f"[check_layering] References scanned: {import_ref_count} import/include, "
        f"{cmake_ref_count} CMake link"
    )
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
                f"  - {rel}:{v.line}: [{v.kind}] {v.reason} (reference='{v.ref}')"
            )

    print(
        "[check_layering] Action: move the dependency downward, introduce a "
        "backend-specific seam (e.g. via runtime composition), or add a "
        "temporary allowlist entry in tools/repo/layering_allowlist.yaml with "
        "task/expires metadata."
    )
    if args.strict:
        print("[check_layering] STRICT MODE: failing.")
        return 1

    print("[check_layering] WARNING MODE: non-fatal.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
