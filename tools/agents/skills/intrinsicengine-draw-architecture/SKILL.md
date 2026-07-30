---
name: intrinsicengine-draw-architecture
description: Draw bounded, evidence-backed architecture diagrams for IntrinsicEngine, including exact audit views and polished ELK-styled presentation/focus views. Use when a user asks to visualize or map engine layers, improve a diagram's visual layout, focus a subsystem, inspect C++23 module dependencies or upstream change impact, show composition/wiring, runtime flows, data flow, or how a file/module fits the larger system; also trigger on requests for Mermaid, UML, C4-style, dependency, sequence, or architecture diagrams of this repository.
---

# Draw IntrinsicEngine Architecture

Choose the smallest view that answers the question, derive every relationship
from repository evidence, and return editable Mermaid source. Treat C4 as a
zooming discipline and UML/flow notation as view choices; keep IntrinsicEngine
labels in repository vocabulary (`layer`, `module`, `interface unit`, `seam`,
`composition root`, `backend`).

## Select the view

- **Whole engine or layer ownership:** generate the aggregated `layers` view.
- **One module, file, or blast radius:** generate a bounded `modules` view.
  Use `dependencies` for what the focus imports, `dependents` for change
  impact, or `both` for local context.
- **Connection between distant modules:** query the knowledge graph for a
  shortest path, then draw only that path plus essential composition nodes.
- **Runtime ordering, composition, or data flow:** inspect the concrete source
  and draw a hand-authored sequence or flowchart. Static import edges do not
  establish runtime calls.
- **More than 50 modules or exploratory search:** use Graphify or the
  `knowledge-graph` MCP tools. Do not emit the complete graph as one static
  diagram.

Read [references/view-guide.md](references/view-guide.md) before authoring a
context, sequence, or data-flow view, or when a generated structural view
needs manual refinement.

## Choose the output profile

- Use `--style audit` (the default) when inspecting or reviewing exact
  relationships. Keep every selected edge and every aggregated dependency,
  import-site, and violation count visible.
- Use `--style presentation` for explanations, documentation, and
  reader-facing handoff. Embed Mermaid ELK/neo configuration, use restrained
  node styling, move routine layer-edge counts into evidence comments, and
  keep violation labels visible.
- Add `--focus <layer>` to a layer view when one subsystem is the question.
  Audit output keeps every edge and highlights the layer. Presentation output
  keeps every layer as context but draws only edges incident to the focus plus
  every violation; its comments mark every omitted edge `visible=no`.

For a module view, `--focus` continues to select the module neighbourhood;
`--style presentation` changes layout and styling without changing the
selected nodes or import edges.

## Generate structural views

Build the deterministic graph first:

```bash
python3 tools/repo/build_knowledge_graph.py
```

Generate the layer map:

```bash
python3 \
  tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py \
  layers --output /tmp/intrinsic-layers.mmd
```

Generate a reader-facing layer map or a focused subsystem map:

```bash
python3 \
  tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py \
  layers --style presentation --output /tmp/intrinsic-layers-presentation.mmd

python3 \
  tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py \
  layers --style presentation --focus runtime \
  --output /tmp/intrinsic-runtime-presentation.mmd
```

Generate a focused module neighbourhood:

```bash
python3 \
  tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py \
  modules \
  --focus Extrinsic.Graphics.FrameRecipe \
  --direction both \
  --radius 1 \
  --max-nodes 30 \
  --output /tmp/frame-recipe-neighbourhood.mmd
```

For change impact, use `--direction dependents`. For implementation
dependencies, use `--direction dependencies`. Keep `--radius 1` unless the
result is demonstrably too narrow. `--focus` accepts an exact module name,
graph node ID, exported-module source path, or unambiguous substring. The
renderer fails instead of silently truncating when the selected view exceeds
its node budget.

## Establish evidence

Follow this evidence order:

1. Read `AGENTS.md` and the relevant architecture/ADR documents.
2. Use the generated module graph for `import` relationships only.
3. Run `python3 tools/repo/check_layering.py --root src --strict` before
   presenting a suspected boundary violation. It also covers `#include`
   relationships that the graph omits.
4. Inspect exact composition and call sites for runtime-flow diagrams. Cite
   repository-relative paths and line numbers in the accompanying prose.
5. Mark inferred or intentionally omitted relationships explicitly. Never
   convert a naming similarity into an edge.

## Author dynamic views

Use Mermaid `sequenceDiagram` for ordered interaction and `flowchart` for
ownership or data transformation. Limit a sequence view to one scenario,
approximately eight participants, and twenty messages. If the flow crosses
many layers, show snapshots/descriptors at the boundary rather than internal
implementation detail.

Do not infer a sequence from module imports. Trace the entry point, runtime
composition root, lower-layer call, result/diagnostic path, and any writeback
or presentation step in source.

## Deliver the result

Return:

1. A descriptive title and one-sentence question the view answers.
2. The rendered Mermaid block or a link to the generated `.mmd` file.
3. A legend stating that `A --> B` means “A imports/depends on B” for
   structural views.
4. The style, focus, direction, radius, and node count.
5. The authoritative evidence paths and any blind spots.

If `mmdc` is already installed, render SVG as an additional convenience:

```bash
mmdc -i <diagram>.mmd -o <diagram>.svg
```

Presentation output carries its Mermaid layout/theme configuration in the
`.mmd` file; do not require a separate config file. Treat renderer support as
an optional convenience and keep the editable Mermaid source as the durable
artifact.

Do not install a renderer or commit a generated diagram unless the user asks.
Mermaid source is the durable output; the repository contract and source
remain authoritative.
