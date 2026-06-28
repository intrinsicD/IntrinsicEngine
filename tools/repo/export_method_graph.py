#!/usr/bin/env python3
"""Emit a graphify-compatible ``graph.json`` for the paper -> method -> code chain.

Companion to ``export_module_graph.py``. Where that tool maps the engine's C++23
module dependency graph, this one maps the *method/paper* layer from the
authoritative, deterministic sources under ``methods/``:

- ``methods/**/method.yaml`` -> method nodes (id, domain, status, backends) and
  paper nodes (title, authors, doi, url) via the same loader the manifest
  validator uses (``yaml.safe_load``).
- ``methods/**/paper.md`` -> "paper concept" nodes from the markdown section
  headings (deterministic; no LLM/PDF semantic extraction required).
- ``methods/**/{src,include}`` -> method source-file nodes, and ``import``
  edges to engine modules. Engine-module targets reuse ``export_module_graph``'s
  ``mod__<module>`` id scheme, so ``graphify merge-graphs code.json method.json``
  stitches a method directly onto the real module-dependency graph.

The result lets an agent (or graphify ``path`` / ``explain`` / MCP) answer
"which paper claim does this method implement, and what code/modules realize
it" — entirely from checked-in, authoritative artifacts. The authoritative
record of paper claims remains the method contract (``method.yaml`` + docs);
this graph is a discovery aid, and its output is a build/ artifact, not
committed.

Optional probabilistic augmentation (NOT done here): ``graphify add <paper.pdf>``
with an LLM backend key produces a fuzzy concept graph that can be merged in
via ``graphify merge-graphs``. ``paper.md`` is the no-key deterministic
alternative this tool consumes.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import yaml

# Reuse sibling adapter helpers so module-node ids match across graphs (enables
# graphify merge-graphs to connect method imports to real module nodes), and
# the layering import regex / target detection stay single-sourced.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_module_graph import _primary, _slug  # noqa: E402
from check_layering import IMPORT_RE, detect_target_layer  # noqa: E402

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
HEADING_RE = re.compile(r"^(#{2,4})\s+(.*\S)\s*$")
METHOD_CODE_EXTS = {".cpp", ".cc", ".cxx", ".hpp", ".hxx", ".h", ".cppm", ".ixx"}


def _id(*parts: str) -> str:
    raw = "::".join(parts).lower()
    return re.sub(r"[^a-z0-9]+", "_", raw).strip("_")


def _scalar(value) -> str:
    if value is None:
        return ""
    return str(value).strip()


def build_graph(methods_root: Path, exclude_template: bool) -> dict:
    nodes: list[dict] = []
    links: list[dict] = []
    seen: set[str] = set()
    stats = {
        "methods": 0,
        "papers": 0,
        "concepts": 0,
        "method_code_files": 0,
        "method_to_module_edges": 0,
        "skipped_manifests": 0,
    }

    def add_node(nid: str, label: str, kind: str, **extra) -> str:
        if nid not in seen:
            seen.add(nid)
            node = {
                "id": nid,
                "label": label,
                "file_type": extra.pop("file_type", "doc"),
                "source_file": extra.pop("source_file", ""),
                "source_location": extra.pop("source_location", ""),
                "kind": kind,
                "_origin": "method-graph",
            }
            node.update(extra)
            nodes.append(node)
        return nid

    def add_link(src: str, dst: str, relation: str, **extra) -> None:
        link = {
            "source": src,
            "target": dst,
            "relation": relation,
            "confidence": extra.pop("confidence", "EXTRACTED"),
            "weight": float(extra.pop("weight", 1.0)),
            "source_file": extra.pop("source_file", ""),
            "source_location": extra.pop("source_location", ""),
            "_origin": "method-graph",
        }
        link.update(extra)
        links.append(link)

    for manifest in sorted(methods_root.rglob("method.yaml")):
        rel = manifest.relative_to(methods_root)
        if exclude_template and ("_template" in rel.parts or "_examples" in rel.parts):
            continue
        try:
            data = yaml.safe_load(manifest.read_text(encoding="utf-8")) or {}
        except (OSError, yaml.YAMLError):
            stats["skipped_manifests"] += 1
            continue
        if not isinstance(data, dict) or "id" not in data:
            stats["skipped_manifests"] += 1
            continue

        pkg = manifest.parent
        method_label = _scalar(data.get("name")) or _scalar(data.get("id"))
        method_id = add_node(
            _id("method", _scalar(data["id"])),
            method_label,
            "method",
            file_type="code",
            source_file=manifest.as_posix(),
            source_location="L1",
            domain=_scalar(data.get("domain")),
            status=_scalar(data.get("status")),
        )
        stats["methods"] += 1

        # Paper node from the manifest's paper block.
        paper = data.get("paper") or {}
        if isinstance(paper, dict):
            title = _scalar(paper.get("title")) or f"(paper for {method_label})"
            doi = _scalar(paper.get("doi"))
            paper_id = add_node(
                _id("paper", doi or title),
                title,
                "paper",
                doi=doi,
                url=_scalar(paper.get("url")),
                authors=_scalar(paper.get("authors")),
                year=_scalar(paper.get("year")),
            )
            add_link(method_id, paper_id, "implements")
            stats["papers"] += 1

            # Paper-concept nodes from paper.md headings (deterministic).
            paper_md = pkg / "paper.md"
            if paper_md.exists():
                for lineno, line in enumerate(
                    paper_md.read_text(encoding="utf-8", errors="ignore").splitlines(),
                    start=1,
                ):
                    hm = HEADING_RE.match(line)
                    if not hm:
                        continue
                    heading = hm.group(2).strip()
                    if not heading:
                        continue
                    concept_id = add_node(
                        _id("concept", _scalar(data["id"]), heading),
                        heading,
                        "concept",
                        source_file=paper_md.as_posix(),
                        source_location=f"L{lineno}",
                    )
                    add_link(paper_id, concept_id, "section")
                    stats["concepts"] += 1

        # Backend nodes.
        for backend in data.get("backends") or []:
            b = _scalar(backend)
            if not b:
                continue
            bid = add_node(_id("backend", b), b, "backend")
            add_link(method_id, bid, "has_backend")

        # Method source files + engine-module import edges.
        for sub in ("src", "include"):
            base = pkg / sub
            if not base.is_dir():
                continue
            for code in sorted(base.rglob("*")):
                if not code.is_file() or code.suffix.lower() not in METHOD_CODE_EXTS:
                    continue
                file_id = add_node(
                    _id("file", code.relative_to(methods_root).as_posix()),
                    code.name,
                    "method_code",
                    file_type="code",
                    source_file=code.as_posix(),
                    source_location="L1",
                )
                add_link(method_id, file_id, "contains")
                stats["method_code_files"] += 1

                try:
                    lines = code.read_text(encoding="utf-8", errors="ignore").splitlines()
                except OSError:
                    continue
                for line_no, line in enumerate(lines, start=1):
                    im = IMPORT_RE.match(line)
                    if not im:
                        continue
                    tok = im.group(1).strip()
                    if tok.startswith(":") or tok.startswith("<") or tok.endswith(">"):
                        continue
                    primary = _primary(tok)
                    # Only link tokens that name an engine module/layer; reuse the
                    # code graph's id so a merge connects to the real module node.
                    if detect_target_layer(primary) is None:
                        continue
                    add_link(
                        file_id,
                        _slug(primary),
                        "imports",
                        context="import",
                        source_file=code.as_posix(),
                        source_location=f"L{line_no}",
                    )
                    stats["method_to_module_edges"] += 1

    graph = {
        "directed": True,
        "input_tokens": 0,
        "output_tokens": 0,
        "nodes": nodes,
        "links": links,
    }
    return {"graph": graph, "stats": stats}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("methods"), help="Methods root to scan.")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("build/module-graph/graphify-out/method-graph.json"),
        help="Output graph.json path.",
    )
    parser.add_argument(
        "--include-template",
        action="store_true",
        help="Include _template/_examples packages (default: skip).",
    )
    args = parser.parse_args()

    if not args.root.exists():
        print(f"methods root does not exist: {args.root}", file=sys.stderr)
        return 2

    result = build_graph(args.root, exclude_template=not args.include_template)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result["graph"], indent=2), encoding="utf-8")

    s = result["stats"]
    print(f"wrote {args.out}")
    print(
        f"  methods={s['methods']} papers={s['papers']} concepts={s['concepts']} "
        f"method_code_files={s['method_code_files']} "
        f"method->module_edges={s['method_to_module_edges']}"
    )
    if s["skipped_manifests"]:
        print(f"  note: {s['skipped_manifests']} manifests skipped (unparseable / no id)")
    if s["method_to_module_edges"] == 0:
        print(
            "  note: 0 method->engine-module edges — reference packages are "
            "self-contained today; edges appear as methods integrate engine modules."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
