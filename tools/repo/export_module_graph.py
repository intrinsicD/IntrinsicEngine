#!/usr/bin/env python3
"""Emit a graphify-compatible ``graph.json`` from the C++23 module graph.

graphify (https://github.com/safishamsi/graphify) renders queryable knowledge
graphs, but its tree-sitter C++ extractor cannot parse C++20/23 module syntax
(``export module``, dotted module names, partitions) and skips ``.cppm`` files
entirely. Rather than fork the extractor, this adapter reuses the repository's
own *authoritative, module-aware* parsers as the single source of truth:

- ``generate_module_inventory.MODULE_RE`` for ``export module <name>;``.
- ``check_layering`` for ``import`` extraction, layer ownership, the layer DAG
  (``ALLOWED_DEPS``), and module-prefix -> layer mapping.

The result is a deterministic dependency graph with 100%-correct module edges
that graphify can visualize (``graphify cluster-only``), query (``graphify path``
/ ``explain``), or serve over MCP. The emitted JSON mirrors graphify's schema:
top-level ``nodes`` / ``links``; node ``id`` / ``label`` / ``source_file`` /
``source_location``; link ``source`` / ``target`` / ``relation`` / ``confidence``
/ ``weight``. Extra fields (``layer``, ``kind``, ``layer_relation``) are additive
and ignored by graphify tooling.

This is an agent-side discovery aid, not an authority: the layering *gate*
remains ``check_layering.py``. The generated ``graph.json`` is a build artifact
and must not be committed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

# Reuse the repository's own parsers as the single source of truth.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_layering import (  # noqa: E402
    ALLOWED_DEPS,
    IMPORT_RE,
    SOURCE_EXTS,
    _is_excluded,
    detect_owner_layer,
    detect_target_layer,
)

# A file's owning module: ``export module X;``, ``module X;``, or a partition
# ``X:Part;`` (export or impl unit). Broader than the inventory's export-only
# regex because implementation units also carry import edges.
OWNING_MODULE_RE = re.compile(
    r"^\s*(?:export\s+)?module\s+([A-Za-z0-9_.:]+)\s*;", re.MULTILINE
)
EXPORT_MODULE_RE = re.compile(
    r"^\s*export\s+module\s+([A-Za-z0-9_.:]+)\s*;", re.MULTILINE
)


def _slug(module: str) -> str:
    return "mod__" + module.lower().replace(".", "_").replace(":", "__")


def _primary(module: str) -> str:
    """Collapse a partition ``X:Part`` to its primary module ``X``."""
    return module.split(":", 1)[0].strip()


def collect_source_files(root: Path, exclude: list[str]) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if _is_excluded(path, root, exclude):
            continue
        if path.suffix.lower() in SOURCE_EXTS:
            files.append(path)
    return files


def build_graph(root: Path, exclude: list[str], include_external: bool) -> dict:
    files = collect_source_files(root, exclude)

    # Pass 1: register every exported module declaration -- including real
    # partitions (``X:Part``) -- as its own node, keyed by the *full* module
    # name so partitions never collapse into the primary interface unit. Each
    # file's full owning-module name is recorded so imports attribute to the
    # exact unit, and its primary is kept to resolve ``import :Part;`` shorthand.
    module_file: dict[str, str] = {}
    module_line: dict[str, int] = {}
    module_layer: dict[str, str] = {}
    file_owner: dict[Path, str] = {}    # full owning module name (incl. :Part)
    file_primary: dict[Path, str] = {}  # primary module of that file

    for path in files:
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue

        owner_match = OWNING_MODULE_RE.search(text)
        if owner_match:
            owner_full = owner_match.group(1).strip()
            file_owner[path] = owner_full
            file_primary[path] = _primary(owner_full)

        for m in EXPORT_MODULE_RE.finditer(text):
            full = m.group(1).strip()
            line = text[: m.start()].count("\n") + 1
            if full not in module_file:
                module_file[full] = path.as_posix()
                module_line[full] = line
                module_layer[full] = detect_owner_layer(path) or "unknown"

    # Pass 2: resolve import edges and aggregate at module granularity.
    edge_weight: dict[tuple[str, str], int] = defaultdict(int)
    edge_lines: dict[tuple[str, str], tuple[str, int]] = {}
    external_targets: dict[str, str] = {}  # slug -> label
    external_edges: dict[tuple[str, str], int] = defaultdict(int)
    skipped_no_owner = 0
    unresolved = 0

    for path in files:
        owner = file_owner.get(path)
        if owner is None:
            # Global-module TU with imports we cannot attribute to a node.
            try:
                if IMPORT_RE.search(path.read_text(encoding="utf-8", errors="ignore")):
                    skipped_no_owner += 1
            except OSError:
                pass
            continue

        owner_primary = file_primary.get(path, _primary(owner))
        # Attribute edges to an existing node: the owning unit when it is an
        # exported module/partition, otherwise its primary interface. A non-
        # exported impl unit (e.g. ``module X:Part_impl;``) folds into the
        # primary instead of producing a dangling edge source.
        owner_node = owner if owner in module_file else owner_primary
        if owner_node not in module_file:
            skipped_no_owner += 1
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue

        for line_no, line in enumerate(lines, start=1):
            im = IMPORT_RE.match(line)
            if not im:
                continue
            tok = im.group(1).strip()
            if tok.startswith("<") or tok.endswith(">"):
                target = ("external", tok)
            elif tok.startswith(":"):
                # ``import :Part;`` -> the partition of this file's own primary.
                full = f"{owner_primary}{tok}"
                target = ("module", full) if full in module_file else ("external", tok)
            elif tok in module_file:
                # Whole module or fully-qualified partition ``X:Part``.
                target = ("module", tok)
            elif _primary(tok) == "std" or _primary(tok).startswith("std."):
                target = ("external", tok)
            else:
                target = ("external", tok)
                unresolved += 1

            kind, name = target
            if kind == "module":
                if name == owner_node:
                    continue  # self
                key = (owner_node, name)
                edge_weight[key] += 1
                edge_lines.setdefault(key, (path.as_posix(), line_no))
            elif include_external:
                slug = "ext__" + re.sub(r"[^a-z0-9]+", "_", name.lower())
                external_targets[slug] = name
                external_edges[(_slug(owner_node), slug)] += 1

    # Assemble graphify-schema nodes + links.
    nodes: list[dict] = []
    links: list[dict] = []
    layers_seen: set[str] = set()

    for module in sorted(module_file):
        layer = module_layer.get(module, "unknown")
        layers_seen.add(layer)
        nodes.append(
            {
                "id": _slug(module),
                "label": module,
                "file_type": "code",
                "source_file": module_file[module],
                "source_location": f"L{module_line.get(module, 1)}",
                "layer": layer,
                "kind": "module",
                "_origin": "module-graph",
            }
        )

    # Synthetic layer-group nodes give the HTML viz visible structure without
    # needing graphify's LLM-based community naming.
    for layer in sorted(layers_seen):
        nodes.append(
            {
                "id": f"layer__{layer.replace('/', '_')}",
                "label": f"[layer] {layer}",
                "file_type": "group",
                "source_file": "",
                "source_location": "",
                "layer": layer,
                "kind": "layer",
                "_origin": "module-graph",
            }
        )
    for module in sorted(module_file):
        layer = module_layer.get(module, "unknown")
        links.append(
            {
                "source": f"layer__{layer.replace('/', '_')}",
                "target": _slug(module),
                "relation": "contains",
                "confidence": "EXTRACTED",
                "weight": 1.0,
                "source_file": "",
                "source_location": "",
                "_origin": "module-graph",
            }
        )

    violations = 0
    for (src, dst), weight in sorted(edge_weight.items()):
        src_layer = module_layer.get(src, "unknown")
        dst_layer = module_layer.get(dst, "unknown")
        if src_layer == dst_layer:
            rel = "same-layer"
        elif dst_layer in ALLOWED_DEPS.get(src_layer, set()):
            rel = "allowed"
        else:
            rel = "violation"
            violations += 1
        sf, sl = edge_lines[(src, dst)]
        links.append(
            {
                "source": _slug(src),
                "target": _slug(dst),
                "relation": "imports",
                "context": "import",
                "confidence": "EXTRACTED",
                "weight": float(weight),
                "source_file": sf,
                "source_location": f"L{sl}",
                "layer_relation": rel,
                "_origin": "module-graph",
            }
        )

    if include_external:
        for slug, label in sorted(external_targets.items()):
            nodes.append(
                {
                    "id": slug,
                    "label": label,
                    "file_type": "external",
                    "source_file": "",
                    "source_location": "",
                    "kind": "external",
                    "_origin": "module-graph",
                }
            )
        for (src, dst), weight in sorted(external_edges.items()):
            links.append(
                {
                    "source": src,
                    "target": dst,
                    "relation": "imports",
                    "context": "import",
                    "confidence": "INFERRED",
                    "weight": float(weight),
                    "source_file": "",
                    "source_location": "",
                    "_origin": "module-graph",
                }
            )

    graph = {
        "directed": True,
        "input_tokens": 0,
        "output_tokens": 0,
        "nodes": nodes,
        "links": links,
    }
    stats = {
        "modules": len(module_file),
        "module_edges": len(edge_weight),
        "layer_violation_edges": violations,
        "external_targets": len(external_targets) if include_external else 0,
        "files_scanned": len(files),
        "import_edges_no_owner_skipped": skipped_no_owner,
        "unresolved_module_imports": unresolved,
        "layers": sorted(layers_seen),
    }
    return {"graph": graph, "stats": stats}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("src"), help="Source root to scan.")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("build/module-graph/graphify-out/graph.json"),
        help="Output graph.json path (graphify reads <dir>/graphify-out/graph.json).",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="PATTERN",
        help="Skip files whose path (relative to --root) matches PATTERN. Repeatable.",
    )
    parser.add_argument(
        "--include-external",
        action="store_true",
        help="Include std/header import targets as external nodes (default: off).",
    )
    args = parser.parse_args()

    if not args.root.exists():
        print(f"source root does not exist: {args.root}", file=sys.stderr)
        return 2

    result = build_graph(args.root, args.exclude, args.include_external)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result["graph"], indent=2), encoding="utf-8")

    s = result["stats"]
    print(f"wrote {args.out}")
    print(
        f"  modules={s['modules']} edges={s['module_edges']} "
        f"violations={s['layer_violation_edges']} "
        f"external={s['external_targets']} files={s['files_scanned']}"
    )
    print(f"  layers: {', '.join(s['layers'])}")
    if s["unresolved_module_imports"]:
        print(f"  note: {s['unresolved_module_imports']} non-std imports did not resolve to a known module")
    if s["import_edges_no_owner_skipped"]:
        print(f"  note: {s['import_edges_no_owner_skipped']} files with imports had no owning module decl")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
