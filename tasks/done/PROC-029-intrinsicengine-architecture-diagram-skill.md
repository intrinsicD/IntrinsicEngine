---
id: PROC-029
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex"
branch: "agent/proc-029-architecture-diagram-skill"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-07-30T20:19:25Z"
maturity_target: Operational
---
# PROC-029 — IntrinsicEngine architecture diagram skill

## Status

- Completed on 2026-07-30 at `Operational`; owner: Codex.
- Commit reference: the retirement commit containing this task on
  `agent/proc-029-architecture-diagram-skill`; no PR was requested.
- The final 21-case tooling regression, Ruff lint/format, skill validator,
  skill-mirror check, strict task/docs/ARA checks, and live graph generation
  passed.
- Live presentation generation covered all 10 layers and 372 modules. The
  runtime focus retained all 10 layer nodes while projecting 9 of 20
  cross-layer pairs, and the installed Mermaid/Chrome path rendered it to a
  valid SVG.

## Goal

- Add one repository-owned skill that produces evidence-backed, bounded
  architectural diagrams for IntrinsicEngine at layer, module-neighbourhood,
  change-impact, and runtime-flow scales, with separate audit and presentation
  outputs for exact inspection and reader-facing communication.

## Non-goals

- No language-generic or cross-repository extractor in this slice.
- No C++ AST, call-graph, class-diagram, or automatic sequence extraction.
- No browser application, MCP server, diagram registry, hosted service, or
  external rendering dependency.
- No replacement of the existing knowledge graph, module inventory, layering
  checker, or textual `intrinsicengine-zoom-out` skill.
- No claim that module imports capture `#include`, CMake-link, runtime-call, or
  data-flow relationships.

## Context

- The user selected the IntrinsicEngine-only first slice after reviewing C4,
  UML, Mermaid, PlantUML, D2, Graphviz, Structurizr, and the existing
  repository knowledge graph.
- After exercising the first live layer map, the user selected the proposed
  `audit`/`presentation` split and layer focus as the next refinement.
- `tools/repo/build_knowledge_graph.py` already emits deterministic module,
  layer, and method/paper relationships with source provenance. Its current
  artifact is a useful evidence model but is too dense to serve as one static
  architecture diagram.
- `intrinsicengine-zoom-out` asks the right layer/module/composition questions
  but intentionally returns a short text map rather than a visual artifact.
- Mermaid text is the dependency-free committed output for this slice. The
  skill may render an SVG only when an existing renderer is already available;
  Mermaid source remains the durable result.
- The module graph is a navigation aid. `tools/repo/check_layering.py` remains
  authoritative for both imports and includes, and runtime-flow diagrams must
  cite inspected source rather than infer behavior from static imports.

## Control surfaces

- Config: none.
- UI: explicit or implicit invocation of
  `$intrinsicengine-draw-architecture`.
- Agent/CLI: the skill workflow plus a plain Python renderer with view, focus,
  style, direction, radius, node-budget, graph-path, and output-path
  arguments.

## Right-sizing

- **Element:** multi-scale architecture drawing could become a new graph
  framework, model registry, renderer abstraction, or interactive application.
- **Simpler alternative:** keep one focused skill, one plain Python script over
  the existing graph JSON, and one short hand-authored view guide. Emit
  deterministic Mermaid without adding a dependency.
- **Blast radius:** `tools/agents/skills/`, one tooling regression test, the
  skill catalog, task records, and ARA trace. No engine source, module, CMake,
  or layer edge changes.
- **Reintroduction trigger:** add a renderer seam or richer normalized model
  only when a second checked-in output format or a second repository adapter
  is actively required.

## Required changes

- [x] Initialize a standards-compliant
  `intrinsicengine-draw-architecture` skill with concise triggering metadata
  and UI metadata.
- [x] Add a deterministic renderer for an aggregated layer view and a bounded
  module neighbourhood/impact view over the existing graph JSON.
- [x] Make focus resolution, traversal direction, radius, node budgets,
  provenance comments, styling, diagnostics, and output ordering explicit and
  deterministic.
- [x] Define the evidence-first workflow for hand-authored context,
  composition, sequence, and data-flow diagrams that static imports cannot
  establish.
- [x] Keep complete-graph exploration routed to Graphify/knowledge-graph tools
  rather than emitting an unreadable static graph.
- [x] Add deterministic `audit` and `presentation` render profiles while
  preserving the existing audit output as the default.
- [x] Add layer focus that highlights the selected layer in audit output and
  limits presentation edges to its direct dependency context without hiding
  that omission in the evidence metadata.
- [x] Make presentation Mermaid self-contained with restrained styling and
  ELK layout metadata; do not add a renderer dependency.

## Tests

- [x] Add focused regression coverage for layer aggregation, dependency and
  dependent traversal, ambiguous/missing focus diagnostics, node-budget
  failure, escaping, provenance comments, and deterministic output.
- [x] Run representative commands against the live IntrinsicEngine graph and
  inspect the emitted Mermaid source.
- [x] Validate the skill with the skill-creator validator.
- [x] Cover style parsing, audit compatibility, presentation metadata,
  presentation label reduction, focused edge selection, and invalid layer
  diagnostics.
- [x] Exercise both presentation modes against the live graph and render a
  representative SVG with the already-installed `mmdc`.

## Docs

- [x] Add the skill to `tools/agents/skills/README.md` and keep its skill-count
  and category inventory accurate.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and retiring the task.
- [x] Append the completed task narrative to
  `tasks/done/RETIREMENT-LOG.md`.
- [x] Update the skill and view guide with profile/focus selection guidance.
- [x] Refresh task indexes, retirement narrative, and completion evidence
  after the expanded surface is final.

## Acceptance criteria

- [x] `$intrinsicengine-draw-architecture` gives another agent a complete,
  evidence-first workflow for choosing the smallest useful IntrinsicEngine
  diagram.
- [x] Layer and focused module diagrams are reproducible from the same graph
  input, stay within declared scope limits, and distinguish graph evidence
  from source-inspected runtime behavior.
- [x] The implementation introduces no external dependency or parallel
  architecture model and does not weaken the authoritative layering gate.
- [x] Focused tooling tests and strict task/docs/skill/ARA checks pass.
- [x] A complete standard-profile evidence report records the final source
  surface and successful required commands.
- [x] Default audit output remains evidence-complete and backward compatible;
  presentation output explicitly records omitted visible detail in comments.
- [x] Focused presentation output is materially less cluttered while retaining
  every layer as context and every violation as a visible edge.

## Verification

```bash
python3 tests/regression/tooling/Test.ArchitectureDiagramSkill.py
python3 tools/repo/build_knowledge_graph.py
python3 tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py layers
python3 tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py layers --style presentation --focus runtime --output /tmp/intrinsic-runtime-presentation.mmd
python3 tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py modules --focus Extrinsic.Graphics.FrameRecipe --direction both --radius 1
python3 tools/agents/skills/intrinsicengine-draw-architecture/scripts/render_architecture.py modules --focus Extrinsic.Graphics.FrameRecipe --direction both --radius 1 --style presentation --output /tmp/intrinsic-frame-recipe-presentation.mmd
PUPPETEER_EXECUTABLE_PATH=/opt/google/chrome/chrome mmdc -i /tmp/intrinsic-runtime-presentation.mmd -o /tmp/intrinsic-runtime-presentation.svg
python3 /home/alex/.codex/skills/.system/skill-creator/scripts/quick_validate.py tools/agents/skills/intrinsicengine-draw-architecture
python3 tools/agents/sync_skills.py --check
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_ara_claims.py --root . --strict
git diff --check
```

## Forbidden changes

- No engine C++ source, public module surface, layer boundary, runtime wiring,
  method, benchmark, build preset, or dependency-manifest changes.
- No generated diagram committed as an architectural source of truth.
- No silent truncation, invented relationship, or unsupported runtime-flow
  inference.
- No generic plugin/adapter framework ahead of a second concrete consumer.
- No unrelated cleanup or retirement of `intrinsicengine-zoom-out`.
