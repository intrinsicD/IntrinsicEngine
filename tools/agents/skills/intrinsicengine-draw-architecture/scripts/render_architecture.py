#!/usr/bin/env python3
"""Render bounded IntrinsicEngine architecture views as Mermaid source.

The input is the deterministic graph produced by
``tools/repo/build_knowledge_graph.py``. Only C++23 module nodes and ``imports``
links participate in the generated structural views. The script intentionally
does not infer include, CMake-link, runtime-call, ownership, or data-flow
relationships.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import html
import json
import math
import re
import sys
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

DEFAULT_GRAPH = Path("build/knowledge-graph/graphify-out/graph.json")
MAX_STATIC_NODES = 50
MAX_RADIUS = 10

LAYER_ORDER = (
    "core",
    "geometry",
    "assets",
    "ecs",
    "physics",
    "graphics_rhi",
    "graphics",
    "platform",
    "runtime",
    "app",
)

LAYER_STYLES = {
    "core": ("#E2E8F0", "#475569"),
    "geometry": ("#DCFCE7", "#15803D"),
    "assets": ("#FEF3C7", "#B45309"),
    "ecs": ("#FCE7F3", "#BE185D"),
    "physics": ("#EDE9FE", "#6D28D9"),
    "graphics_rhi": ("#DBEAFE", "#1D4ED8"),
    "graphics": ("#CFFAFE", "#0E7490"),
    "platform": ("#FFEDD5", "#C2410C"),
    "runtime": ("#FEE2E2", "#B91C1C"),
    "app": ("#F3E8FF", "#7E22CE"),
}
FALLBACK_STYLE = ("#F1F5F9", "#475569")
LAYER_LABELS = {
    "graphics_rhi": "graphics/rhi",
}
RENDER_STYLES = ("audit", "presentation")
PRESENTATION_FRONTMATTER = (
    "---",
    "config:",
    "  layout: elk",
    "  look: neo",
    "  theme: base",
    "  flowchart:",
    "    curve: step",
    "    nodeSpacing: 70",
    "    rankSpacing: 100",
    "  elk:",
    "    mergeEdges: false",
    "    nodePlacementStrategy: NETWORK_SIMPLEX",
    "    considerModelOrder: NODES_AND_EDGES",
    "  themeVariables:",
    '    fontFamily: "Inter, ui-sans-serif, sans-serif"',
    '    fontSize: "15px"',
    '    lineColor: "#94A3B8"',
    '    primaryTextColor: "#0F172A"',
    "---",
)


class DiagramError(ValueError):
    """An actionable graph or view error."""


@dataclass(frozen=True)
class Node:
    id: str
    label: str
    kind: str
    layer: str
    source_file: str
    source_location: str


@dataclass(frozen=True)
class Link:
    source: str
    target: str
    relation: str
    weight: float
    layer_relation: str
    source_file: str
    source_location: str


@dataclass(frozen=True)
class ModuleGraph:
    nodes: dict[str, Node]
    modules: dict[str, Node]
    imports: tuple[Link, ...]


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DiagramError(f"graph JSON contains duplicate key {key!r}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise DiagramError(f"graph JSON contains non-standard numeric constant {value}")


def _string_field(record: dict[str, Any], field: str, context: str) -> str:
    value = record.get(field)
    if not isinstance(value, str) or not value:
        raise DiagramError(f"{context} requires non-empty string field {field!r}")
    if "\n" in value or "\r" in value:
        raise DiagramError(f"{context} field {field!r} contains a newline")
    return value


def _optional_string(record: dict[str, Any], field: str, context: str) -> str:
    value = record.get(field, "")
    if not isinstance(value, str):
        raise DiagramError(f"{context} field {field!r} must be a string")
    if "\n" in value or "\r" in value:
        raise DiagramError(f"{context} field {field!r} contains a newline")
    return value


def _weight(record: dict[str, Any], context: str) -> float:
    value = record.get("weight", 1.0)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise DiagramError(f"{context} weight must be a positive finite number")
    numeric = float(value)
    if not math.isfinite(numeric) or numeric <= 0.0:
        raise DiagramError(f"{context} weight must be a positive finite number")
    return numeric


def load_graph(path: Path) -> ModuleGraph:
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise DiagramError(
            f"graph not found: {path}; build it with "
            "python3 tools/repo/build_knowledge_graph.py"
        ) from exc
    except (OSError, UnicodeError) as exc:
        raise DiagramError(f"cannot read graph {path}: {exc}") from exc

    try:
        payload = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_constant,
        )
    except json.JSONDecodeError as exc:
        raise DiagramError(
            f"invalid graph JSON at {path}:{exc.lineno}:{exc.colno}: {exc.msg}"
        ) from exc

    if not isinstance(payload, dict):
        raise DiagramError("graph JSON root must be an object")
    raw_nodes = payload.get("nodes")
    raw_links = payload.get("links")
    if not isinstance(raw_nodes, list) or not isinstance(raw_links, list):
        raise DiagramError("graph JSON requires array fields 'nodes' and 'links'")

    nodes: dict[str, Node] = {}
    for index, raw in enumerate(raw_nodes):
        context = f"node[{index}]"
        if not isinstance(raw, dict):
            raise DiagramError(f"{context} must be an object")
        node = Node(
            id=_string_field(raw, "id", context),
            label=_string_field(raw, "label", context),
            kind=_string_field(raw, "kind", context),
            layer=_optional_string(raw, "layer", context) or "unknown",
            source_file=_optional_string(raw, "source_file", context),
            source_location=_optional_string(raw, "source_location", context),
        )
        if node.id in nodes:
            raise DiagramError(f"duplicate graph node id {node.id!r}")
        nodes[node.id] = node

    links: list[Link] = []
    for index, raw in enumerate(raw_links):
        context = f"link[{index}]"
        if not isinstance(raw, dict):
            raise DiagramError(f"{context} must be an object")
        link = Link(
            source=_string_field(raw, "source", context),
            target=_string_field(raw, "target", context),
            relation=_string_field(raw, "relation", context),
            weight=_weight(raw, context),
            layer_relation=_optional_string(raw, "layer_relation", context),
            source_file=_optional_string(raw, "source_file", context),
            source_location=_optional_string(raw, "source_location", context),
        )
        if link.source not in nodes or link.target not in nodes:
            raise DiagramError(
                f"{context} references missing endpoint "
                f"{link.source!r} -> {link.target!r}"
            )
        links.append(link)

    modules = {
        node_id: node for node_id, node in nodes.items() if node.kind == "module"
    }
    imports = tuple(
        link
        for link in links
        if link.relation == "imports"
        and link.source in modules
        and link.target in modules
    )
    if not modules:
        raise DiagramError("graph contains no module nodes")
    if not imports:
        raise DiagramError("graph contains no module import links")
    return ModuleGraph(nodes=nodes, modules=modules, imports=imports)


def _layer_sort_key(layer: str) -> tuple[int, str]:
    try:
        return (LAYER_ORDER.index(layer), layer)
    except ValueError:
        return (len(LAYER_ORDER), layer.casefold())


def _node_sort_key(node: Node) -> tuple[tuple[int, str], str, str]:
    return (_layer_sort_key(node.layer), node.label.casefold(), node.id)


def _safe_id(prefix: str, value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_]+", "_", value).strip("_").lower()
    if not slug:
        slug = "node"
    digest = hashlib.sha256(value.encode("utf-8")).hexdigest()[:8]
    return f"{prefix}_{slug[:48]}_{digest}"


def _layer_class(layer: str) -> str:
    return _safe_id("layer", layer)


def _layer_label(layer: str) -> str:
    return LAYER_LABELS.get(layer, layer)


def _label(value: str) -> str:
    return html.escape(value, quote=True)


def _comment(value: str) -> str:
    return " ".join(value.replace("\r", " ").replace("\n", " ").split())


def _source_ref(node: Node) -> str:
    if not node.source_file:
        return "[source unavailable]"
    if node.source_location:
        return f"{node.source_file}:{node.source_location}"
    return node.source_file


def _format_count(value: float) -> str:
    if value.is_integer():
        return str(int(value))
    return f"{value:.3f}".rstrip("0").rstrip(".")


def _quantity(value: float, singular: str, plural: str) -> str:
    noun = singular if value == 1.0 else plural
    return f"{_format_count(value)} {noun}"


def _header(graph_path: Path, view: str, detail: str) -> list[str]:
    return [
        "%% Generated IntrinsicEngine architecture view; source remains authoritative.",
        f"%% graph: {_comment(graph_path.as_posix())}",
        "%% evidence: C++23 module nodes and import edges only.",
        "%% blind spots: #include, CMake links, runtime calls, ownership, and data flow.",
        f"%% view: {view}; {_comment(detail)}",
    ]


def _class_definitions(layers: Iterable[str]) -> list[str]:
    result: list[str] = []
    for layer in sorted(set(layers), key=_layer_sort_key):
        fill, stroke = LAYER_STYLES.get(layer, FALLBACK_STYLE)
        result.append(
            f"    classDef {_layer_class(layer)} "
            f"fill:{fill},stroke:{stroke},color:#0F172A,stroke-width:2px;"
        )
    return result


def _presentation_class_definitions() -> list[str]:
    return [
        "    classDef context "
        "fill:#F8FAFC,stroke:#CBD5E1,color:#334155,stroke-width:1.5px;",
        "    classDef related "
        "fill:#F1F5F9,stroke:#64748B,color:#1E293B,stroke-width:2px;",
        "    classDef focus "
        "fill:#EFF6FF,stroke:#2563EB,color:#0F172A,stroke-width:4px;",
    ]


def render_layers(
    graph: ModuleGraph,
    graph_path: Path,
    orientation: str,
    style: str = "audit",
    focus_layer: str | None = None,
) -> str:
    if style not in RENDER_STYLES:
        raise DiagramError(
            f"unknown render style {style!r}; choose from {', '.join(RENDER_STYLES)}"
        )

    layer_modules: dict[str, list[Node]] = defaultdict(list)
    for node in graph.modules.values():
        layer_modules[node.layer].append(node)

    aggregates: dict[tuple[str, str], dict[str, float]] = defaultdict(
        lambda: {"dependencies": 0.0, "sites": 0.0, "violations": 0.0}
    )
    for link in graph.imports:
        source_layer = graph.modules[link.source].layer
        target_layer = graph.modules[link.target].layer
        if source_layer == target_layer:
            continue
        bucket = aggregates[(source_layer, target_layer)]
        bucket["dependencies"] += 1.0
        bucket["sites"] += link.weight
        if link.layer_relation == "violation":
            bucket["violations"] += 1.0

    layers = sorted(layer_modules, key=_layer_sort_key)
    if focus_layer is not None and focus_layer not in layer_modules:
        raise DiagramError(f"focus layer {focus_layer!r} is absent from the graph")

    aggregate_items = sorted(
        aggregates.items(),
        key=lambda item: (
            _layer_sort_key(item[0][0]),
            _layer_sort_key(item[0][1]),
        ),
    )
    visible_items = aggregate_items
    if style == "presentation" and focus_layer is not None:
        visible_items = [
            item
            for item in aggregate_items
            if focus_layer in item[0] or item[1]["violations"] > 0.0
        ]

    if style == "audit" and focus_layer is None:
        detail = (
            f"layers={len(layers)} modules={len(graph.modules)} "
            f"cross_layer_pairs={len(aggregates)}; A --> B means A imports B"
        )
    elif style == "presentation":
        focus_label = _layer_label(focus_layer) if focus_layer is not None else "none"
        projection = "all" if focus_layer is None else "focus-incident-plus-violations"
        detail = (
            f"style=presentation focus={focus_label} layers={len(layers)} "
            f"modules={len(graph.modules)} cross_layer_pairs={len(aggregates)} "
            f"visible_pairs={len(visible_items)} projection={projection} "
            "edge_labels=violations-only; A --> B means A imports B"
        )
    else:
        detail = (
            f"style=audit focus={_layer_label(focus_layer)} layers={len(layers)} "
            f"modules={len(graph.modules)} cross_layer_pairs={len(aggregates)} "
            f"visible_pairs={len(visible_items)}; A --> B means A imports B"
        )

    lines = list(PRESENTATION_FRONTMATTER) if style == "presentation" else []
    lines.extend(_header(graph_path, "layers", detail))
    lines.extend((f"flowchart {orientation}",))

    layer_ids = {layer: _safe_id("layernode", layer) for layer in layers}
    for layer in layers:
        count = len(layer_modules[layer])
        noun = "module" if count == 1 else "modules"
        lines.append(
            f'    {layer_ids[layer]}["{_label(_layer_label(layer))}'
            f'<br/>{count} {noun}"]'
        )

    violation_indices: list[int] = []
    focus_edge_indices: list[int] = []
    for edge_index, ((source, target), counts) in enumerate(visible_items):
        if style == "audit":
            edge_label = (
                f"{_quantity(counts['dependencies'], 'dep', 'deps')} / "
                f"{_quantity(counts['sites'], 'site', 'sites')}"
            )
            if counts["violations"]:
                edge_label += (
                    f" / {_quantity(counts['violations'], 'violation', 'violations')}"
                )
            edge = f'    {layer_ids[source]} -->|"{edge_label}"| {layer_ids[target]}'
        elif counts["violations"]:
            edge_label = _quantity(
                counts["violations"],
                "violation",
                "violations",
            )
            edge = f'    {layer_ids[source]} -->|"{edge_label}"| {layer_ids[target]}'
        else:
            edge = f"    {layer_ids[source]} --> {layer_ids[target]}"
        lines.append(edge)

        if counts["violations"]:
            violation_indices.append(edge_index)
        elif focus_layer is not None and focus_layer in {source, target}:
            focus_edge_indices.append(edge_index)

    if style == "audit":
        lines.extend(_class_definitions(layers))
        for layer in layers:
            lines.append(f"    class {layer_ids[layer]} {_layer_class(layer)};")
        if focus_layer is not None:
            lines.append(
                "    classDef focus "
                "fill:#FFFFFF,stroke:#111827,color:#111827,stroke-width:4px;"
            )
            lines.append(f"    class {layer_ids[focus_layer]} focus;")
    else:
        lines.extend(_presentation_class_definitions())
        related_layers: set[str] = set()
        if focus_layer is not None:
            for (source, target), _counts in aggregate_items:
                if source == focus_layer:
                    related_layers.add(target)
                elif target == focus_layer:
                    related_layers.add(source)
        for layer in layers:
            node_class = "context"
            if layer == focus_layer:
                node_class = "focus"
            elif layer in related_layers:
                node_class = "related"
            lines.append(f"    class {layer_ids[layer]} {node_class};")

    for edge_index in violation_indices:
        lines.append(
            f"    linkStyle {edge_index} stroke:#DC2626,stroke-width:3px,color:#991B1B;"
        )
    if style == "presentation":
        violation_set = set(violation_indices)
        focus_edge_set = set(focus_edge_indices)
        for edge_index in range(len(visible_items)):
            if edge_index in violation_set:
                continue
            if edge_index in focus_edge_set:
                lines.append(
                    f"    linkStyle {edge_index} stroke:#2563EB,stroke-width:2.5px;"
                )
            else:
                lines.append(
                    f"    linkStyle {edge_index} stroke:#94A3B8,stroke-width:1.5px;"
                )

        visible_pairs = {pair for pair, _counts in visible_items}
        for (source, target), counts in aggregate_items:
            visible = "yes" if (source, target) in visible_pairs else "no"
            lines.append(
                f"%% edge evidence: {_comment(_layer_label(source))} -> "
                f"{_comment(_layer_label(target))} "
                f"deps={_format_count(counts['dependencies'])} "
                f"sites={_format_count(counts['sites'])} "
                f"violations={_format_count(counts['violations'])} "
                f"visible={visible}"
            )
    for layer in layers:
        lines.append(
            f"%% layer evidence: {_comment(_layer_label(layer))} "
            f"modules={len(layer_modules[layer])}"
        )
    return "\n".join(lines) + "\n"


def resolve_focus(graph: ModuleGraph, query: str) -> Node:
    query = query.strip()
    if not query:
        raise DiagramError("--focus cannot be empty")
    modules = sorted(graph.modules.values(), key=_node_sort_key)

    exact = [
        node for node in modules if query in {node.id, node.label, node.source_file}
    ]
    if len(exact) == 1:
        return exact[0]

    folded = query.casefold()
    casefold_exact = [
        node
        for node in modules
        if folded
        in {
            node.id.casefold(),
            node.label.casefold(),
            node.source_file.casefold(),
        }
    ]
    if len(casefold_exact) == 1:
        return casefold_exact[0]

    contains = [
        node
        for node in modules
        if folded in node.id.casefold()
        or folded in node.label.casefold()
        or folded in node.source_file.casefold()
    ]
    if len(contains) == 1:
        return contains[0]
    if contains:
        names = ", ".join(node.label for node in contains[:10])
        suffix = "" if len(contains) <= 10 else f", ... ({len(contains)} matches)"
        raise DiagramError(f"focus {query!r} is ambiguous; matches: {names}{suffix}")

    labels = [node.label for node in modules]
    suggestions = difflib.get_close_matches(query, labels, n=5, cutoff=0.3)
    hint = f"; closest modules: {', '.join(suggestions)}" if suggestions else ""
    raise DiagramError(f"focus module {query!r} was not found{hint}")


def resolve_layer(graph: ModuleGraph, query: str) -> str:
    query = query.strip()
    if not query:
        raise DiagramError("--focus cannot be empty")

    layers = sorted(
        {node.layer for node in graph.modules.values()}, key=_layer_sort_key
    )
    folded = query.casefold()
    exact = [
        layer
        for layer in layers
        if folded in {layer.casefold(), _layer_label(layer).casefold()}
    ]
    if len(exact) == 1:
        return exact[0]

    contains = [
        layer
        for layer in layers
        if folded in layer.casefold() or folded in _layer_label(layer).casefold()
    ]
    if len(contains) == 1:
        return contains[0]
    if contains:
        names = ", ".join(_layer_label(layer) for layer in contains)
        raise DiagramError(f"focus layer {query!r} is ambiguous; matches: {names}")

    labels = [_layer_label(layer) for layer in layers]
    suggestions = difflib.get_close_matches(query, labels, n=3, cutoff=0.3)
    hint = f"; closest layers: {', '.join(suggestions)}" if suggestions else ""
    available = ", ".join(labels)
    raise DiagramError(
        f"focus layer {query!r} was not found{hint}; available layers: {available}"
    )


def select_modules(
    graph: ModuleGraph,
    focus: Node,
    direction: str,
    radius: int,
    max_nodes: int,
) -> tuple[list[Node], list[Link]]:
    if radius < 0 or radius > MAX_RADIUS:
        raise DiagramError(f"--radius must be between 0 and {MAX_RADIUS}")
    if max_nodes < 1 or max_nodes > MAX_STATIC_NODES:
        raise DiagramError(
            f"--max-nodes must be between 1 and {MAX_STATIC_NODES}; "
            "use Graphify for broader exploration"
        )

    outgoing: dict[str, set[str]] = defaultdict(set)
    incoming: dict[str, set[str]] = defaultdict(set)
    for link in graph.imports:
        outgoing[link.source].add(link.target)
        incoming[link.target].add(link.source)

    selected = {focus.id}
    queue: deque[tuple[str, int]] = deque(((focus.id, 0),))
    while queue:
        node_id, depth = queue.popleft()
        if depth >= radius:
            continue
        neighbours: set[str] = set()
        if direction in {"dependencies", "both"}:
            neighbours.update(outgoing.get(node_id, set()))
        if direction in {"dependents", "both"}:
            neighbours.update(incoming.get(node_id, set()))
        for neighbour in sorted(
            neighbours,
            key=lambda item: (
                graph.modules[item].label.casefold(),
                graph.modules[item].id,
            ),
        ):
            if neighbour not in selected:
                selected.add(neighbour)
                queue.append((neighbour, depth + 1))

    if len(selected) > max_nodes:
        next_step = (
            f"raise --max-nodes up to {MAX_STATIC_NODES}"
            if max_nodes < MAX_STATIC_NODES
            else "use Graphify for broader exploration"
        )
        raise DiagramError(
            f"view selects {len(selected)} modules, exceeding --max-nodes "
            f"{max_nodes}; reduce --radius, choose dependencies/dependents, "
            f"or {next_step}"
        )

    nodes = sorted((graph.modules[node_id] for node_id in selected), key=_node_sort_key)
    links = sorted(
        (
            link
            for link in graph.imports
            if link.source in selected and link.target in selected
        ),
        key=lambda link: (
            graph.modules[link.source].label.casefold(),
            graph.modules[link.target].label.casefold(),
            link.source,
            link.target,
        ),
    )
    return nodes, links


def render_modules(
    graph: ModuleGraph,
    graph_path: Path,
    focus: Node,
    direction: str,
    radius: int,
    max_nodes: int,
    orientation: str,
    style: str = "audit",
) -> str:
    if style not in RENDER_STYLES:
        raise DiagramError(
            f"unknown render style {style!r}; choose from {', '.join(RENDER_STYLES)}"
        )

    nodes, links = select_modules(
        graph,
        focus=focus,
        direction=direction,
        radius=radius,
        max_nodes=max_nodes,
    )
    layers = sorted({node.layer for node in nodes}, key=_layer_sort_key)
    node_ids = {node.id: _safe_id("module", node.id) for node in nodes}

    detail = (
        f"focus={focus.label} direction={direction} radius={radius} "
        f"nodes={len(nodes)} edges={len(links)}; A --> B means A imports B"
    )
    if style == "presentation":
        detail = f"style=presentation {detail}"

    lines = list(PRESENTATION_FRONTMATTER) if style == "presentation" else []
    lines.extend(_header(graph_path, "modules", detail))
    lines.append(f"flowchart {orientation}")

    for layer in layers:
        subgraph_id = _safe_id("group", layer)
        lines.append(f'    subgraph {subgraph_id}["{_label(_layer_label(layer))}"]')
        lines.append("        direction TB")
        for node in (item for item in nodes if item.layer == layer):
            lines.append(f'        {node_ids[node.id]}["{_label(node.label)}"]')
        lines.append("    end")

    violation_indices: list[int] = []
    for edge_index, link in enumerate(links):
        lines.append(f"    {node_ids[link.source]} --> {node_ids[link.target]}")
        if link.layer_relation == "violation":
            violation_indices.append(edge_index)

    if style == "audit":
        lines.extend(_class_definitions(layers))
        lines.append(
            "    classDef focus "
            "fill:#FFFFFF,stroke:#111827,color:#111827,stroke-width:4px;"
        )
        for node in nodes:
            lines.append(f"    class {node_ids[node.id]} {_layer_class(node.layer)};")
    else:
        lines.extend(_presentation_class_definitions())
        for node in nodes:
            lines.append(f"    class {node_ids[node.id]} related;")
    lines.append(f"    class {node_ids[focus.id]} focus;")

    for edge_index in violation_indices:
        lines.append(
            f"    linkStyle {edge_index} stroke:#DC2626,stroke-width:3px,color:#991B1B;"
        )
    if style == "presentation":
        violation_set = set(violation_indices)
        for edge_index in range(len(links)):
            if edge_index not in violation_set:
                lines.append(
                    f"    linkStyle {edge_index} stroke:#94A3B8,stroke-width:1.5px;"
                )
    for node in nodes:
        lines.append(
            f"%% source: {_comment(node.label)} -> {_comment(_source_ref(node))}"
        )
    return "\n".join(lines) + "\n"


def _add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--graph",
        type=Path,
        default=DEFAULT_GRAPH,
        help=f"Knowledge graph JSON (default: {DEFAULT_GRAPH}).",
    )
    parser.add_argument(
        "--output",
        default="-",
        help="Output .mmd path, or '-' for stdout (default: '-').",
    )
    parser.add_argument(
        "--orientation",
        choices=("LR", "TB"),
        default="LR",
        help="Mermaid flow direction (default: LR).",
    )
    parser.add_argument(
        "--style",
        choices=RENDER_STYLES,
        default="audit",
        help=(
            "Audit keeps complete evidence labels; presentation favors "
            "reader-facing layout and restrained labels (default: audit)."
        ),
    )


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="view", required=True)

    layers = subparsers.add_parser(
        "layers",
        help="Aggregate module imports into a layer dependency map.",
    )
    _add_common_arguments(layers)
    layers.set_defaults(orientation="TB")
    layers.add_argument(
        "--focus",
        help=(
            "Optional exact layer key/name or unambiguous substring. "
            "Presentation shows its incident edges plus all violations."
        ),
    )

    modules = subparsers.add_parser(
        "modules",
        help="Render a bounded module neighbourhood or impact view.",
    )
    _add_common_arguments(modules)
    modules.add_argument(
        "--focus",
        required=True,
        help="Exact module label/id/source path or an unambiguous substring.",
    )
    modules.add_argument(
        "--direction",
        choices=("dependencies", "dependents", "both"),
        default="both",
        help="Traversal direction relative to the focus (default: both).",
    )
    modules.add_argument(
        "--radius",
        type=int,
        default=1,
        help=f"Graph distance from the focus, 0-{MAX_RADIUS} (default: 1).",
    )
    modules.add_argument(
        "--max-nodes",
        type=int,
        default=30,
        help=f"Fail above this budget, at most {MAX_STATIC_NODES} (default: 30).",
    )
    return parser.parse_args(argv)


def write_output(text: str, output: str) -> None:
    if output == "-":
        sys.stdout.write(text)
        return
    path = Path(output)
    if path.exists() and path.is_dir():
        raise DiagramError(f"output path is a directory: {path}")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    except OSError as exc:
        raise DiagramError(f"cannot write output {path}: {exc}") from exc
    print(f"wrote {path}", file=sys.stderr)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        graph = load_graph(args.graph)
        if args.view == "layers":
            focus_layer = (
                resolve_layer(graph, args.focus) if args.focus is not None else None
            )
            text = render_layers(
                graph,
                graph_path=args.graph,
                orientation=args.orientation,
                style=args.style,
                focus_layer=focus_layer,
            )
        else:
            focus = resolve_focus(graph, args.focus)
            text = render_modules(
                graph,
                graph_path=args.graph,
                focus=focus,
                direction=args.direction,
                radius=args.radius,
                max_nodes=args.max_nodes,
                orientation=args.orientation,
                style=args.style,
            )
        write_output(text, args.output)
    except DiagramError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
