#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections import Counter
from datetime import date

MODULE_RE = re.compile(r"^\s*export\s+module\s+([^;\s]+)\s*;", re.MULTILINE)

SRC_LAYER_BY_TOP = {
    "core": "core",
    "geometry": "geometry",
    "assets": "assets",
    "ecs": "ecs",
    "runtime": "runtime",
    "platform": "platform",
    "app": "app",
}

SRC_NEW_LAYER_BY_TOP = {
    "Core": "core",
    "Geometry": "geometry",
    "Assets": "assets",
    "ECS": "ecs",
    "Runtime": "runtime",
    "Platform": "platform",
    "App": "app",
    "Graphics": "graphics",
}


def detect_layer(path: pathlib.Path, root: pathlib.Path) -> str:
    rel = path.relative_to(root)
    if not rel.parts:
        return "unknown"

    if root.name == "src_new":
        return SRC_NEW_LAYER_BY_TOP.get(rel.parts[0], "legacy")

    if root.name == "src":
        top = rel.parts[0]
        if top == "legacy":
            return "legacy"
        if top == "graphics":
            if len(rel.parts) >= 2:
                sub = rel.parts[1]
                if sub in {"rhi", "vulkan", "framegraph", "renderer", "assets"}:
                    return f"graphics/{sub}"
            return "graphics"
        return SRC_LAYER_BY_TOP.get(top, "legacy")

    return rel.parts[0]


def collect_modules(root: pathlib.Path):
    rows = []
    for path in sorted(root.rglob("*.cppm")):
        text = path.read_text(encoding="utf-8")
        match = MODULE_RE.search(text)
        if not match:
            continue
        rows.append(
            {
                "module": match.group(1),
                "file": path.as_posix(),
                "layer": detect_layer(path, root),
            }
        )
    return rows


def render(rows: list[dict[str, str]], root: pathlib.Path) -> str:
    lines: list[str] = []
    lines.append("# Module Inventory (auto-generated)")
    lines.append("")
    lines.append(f"_Generated on {date.today().isoformat()} by `tools/repo/generate_module_inventory.py`._")
    lines.append("")
    lines.append(f"Root scanned: `{root.as_posix()}`")
    lines.append("")

    layer_counts = Counter(row["layer"] for row in rows)
    lines.append("## Layer Summary")
    lines.append("")
    lines.append("| Layer | Module Count |")
    lines.append("|---|---:|")
    for layer in sorted(layer_counts):
        lines.append(f"| `{layer}` | {layer_counts[layer]} |")
    lines.append("")

    lines.append("## Modules")
    lines.append("")
    lines.append("| Module | File | Layer |")
    lines.append("|---|---|---|")
    for row in rows:
        lines.append(f"| `{row['module']}` | `{row['file']}` | `{row['layer']}` |")
    lines.append("")
    lines.append(f"Total modules: **{len(rows)}**")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate C++ module inventory from a source root.")
    parser.add_argument("--root", default="src", help="Source root to scan (default: src).")
    parser.add_argument(
        "--out",
        default="docs/api/generated/module_inventory.md",
        help="Output markdown path (default: docs/api/generated/module_inventory.md).",
    )
    parser.add_argument("--check", action="store_true", help="Check output file freshness instead of writing.")
    args = parser.parse_args()

    root = pathlib.Path(args.root)
    if not root.exists():
        print(f"source root does not exist: {root}", file=sys.stderr)
        return 2

    rows = collect_modules(root)
    output = render(rows, root)
    out_path = pathlib.Path(args.out)

    if args.check:
        if not out_path.exists():
            print(f"missing inventory file: {out_path}", file=sys.stderr)
            return 1
        if out_path.read_text(encoding="utf-8") != output:
            print(f"inventory drift detected: {out_path}", file=sys.stderr)
            return 3
        print(f"inventory up-to-date: {out_path}")
        return 0

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(output, encoding="utf-8")
    print(f"wrote {out_path} ({len(rows)} modules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
