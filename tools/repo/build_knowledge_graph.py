#!/usr/bin/env python3
"""Build the unified IntrinsicEngine knowledge graph (code + method/paper).

Orchestrates the two deterministic adapters and merges them into a single
graphify-schema ``graph.json`` that graphify can visualize, query
(``graphify path`` / ``explain``), or serve over MCP (see ``.mcp.json``):

- ``export_module_graph.build_graph`` — C++23 module dependency graph from
  ``src/`` (reuses the repository's own module/layering parsers).
- ``export_method_graph.build_graph`` — paper -> method -> code graph from
  ``methods/`` manifests, ``paper.md`` headings, and method sources.

The merge is a plain id-keyed union performed here in Python, so producing the
graph needs **no graphify install and no API key**; graphify is only required to
*view* or *serve* the result. Method-graph import edges target the module
graph's ``mod__<module>`` ids, so the union connects methods directly onto the
module DAG.

This is an agent-side discovery aid, not an authority: the layering gate remains
``check_layering.py`` and paper claims remain authoritative in method contracts.
Output is a build/ artifact and is not committed.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_method_graph  # noqa: E402
import export_module_graph  # noqa: E402


def merge(*graphs: dict) -> dict:
    nodes: dict[str, dict] = {}
    links: list[dict] = []
    seen_links: set[tuple[str, str, str]] = set()
    for g in graphs:
        for n in g["nodes"]:
            nodes.setdefault(n["id"], n)
        for l in g["links"]:
            key = (l["source"], l["target"], l.get("relation", ""))
            if key in seen_links:
                continue
            seen_links.add(key)
            links.append(l)
    return {
        "directed": True,
        "input_tokens": 0,
        "output_tokens": 0,
        "nodes": list(nodes.values()),
        "links": links,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, default=Path("src"), help="C++ source root.")
    parser.add_argument("--methods", type=Path, default=Path("methods"), help="Methods root.")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("build/knowledge-graph/graphify-out/graph.json"),
        help="Output merged graph.json (the path .mcp.json serves).",
    )
    parser.add_argument(
        "--include-external",
        action="store_true",
        help="Include std/header import targets as external nodes.",
    )
    args = parser.parse_args()

    if not args.src.exists():
        print(f"src root does not exist: {args.src}", file=sys.stderr)
        return 2

    code = export_module_graph.build_graph(args.src, [], args.include_external)
    if args.methods.exists():
        methods = export_method_graph.build_graph(args.methods, exclude_template=True)
    else:
        methods = {"graph": {"nodes": [], "links": []}, "stats": {}}

    merged = merge(code["graph"], methods["graph"])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(merged, indent=2), encoding="utf-8")

    cs, ms = code["stats"], methods["stats"]
    print(f"wrote {args.out}")
    print(f"  nodes={len(merged['nodes'])} links={len(merged['links'])}")
    print(
        f"  code: modules={cs.get('modules', 0)} edges={cs.get('module_edges', 0)} "
        f"violations={cs.get('layer_violation_edges', 0)}"
    )
    print(
        f"  methods: methods={ms.get('methods', 0)} papers={ms.get('papers', 0)} "
        f"concepts={ms.get('concepts', 0)} method->module={ms.get('method_to_module_edges', 0)}"
    )
    print("  view:  cd $(dirname {}); graphify cluster-only . --no-label".format(args.out.parent))
    print(f"  query: graphify explain \"<module-or-paper>\" --graph {args.out}")
    print("  serve: graphify-mcp --graph {} (or use the repo .mcp.json)".format(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
