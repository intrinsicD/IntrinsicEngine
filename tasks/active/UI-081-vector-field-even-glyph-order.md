---
id: UI-081
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, CI and the pre-flight-reviewed method comparison
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence]
---
# UI-081 — Evenly spread vector-field glyphs from a progressive element order

## Goal
- Appearance vector fields draw their arrows evenly over the element domain instead of every n-th
  element in index order: a per-domain progressive order of the elements (vertices / graph nodes /
  points, edges, faces) computed on demand, with an arrow-count slider that takes a prefix of it.
- The ordering method is chosen by a pre-flight-reviewed comparison of the existing
  `Geometry.PointSampling` methods on mesh element anchors.

## Operator questions (verbatim, 2026-10-22)
1. "wenn ich vertex vectorfelder rendere in Appearance dann kann ich pro vectorfeld ja auswählen dass der vector nur every nth element gerendert werden soll. das feature ist super abder die umsetzung stört mich da die verteilung nicht gleichmässig über die domäne verteilt ist."
2. "1. möchte ich auf meshes für jede sampling methode testen welche von denen eine domäne am gleichmässigsten verteilt sampled, indem wir uns die verteilung der voronoi areas für jedes sample auf einem mesh anschauen. (ist das hier der richtige test))"
3. "2. die gleichmässigste methode (wenn die schnell genug ist für die entsprechended daten)  soll gewählt werden um dann bei "nth element" als base points für die vectoren zu gelten. am besten wird da auch eine methode gewählt die nicht immer neu berechnet werden muss sondern progressiv punkte hinzu fügt und entfernen kann. so kann man ganz wunderbar weniger vektoren rendern und hat diese immernoch schön über die domäne verteilt. ausserdem soll das per slider passieren."
4. "3. für vertex vektorfelder wird extra so ein sampling bestimmt welches wir explizit per button neu berechnen müssen wenn es nötig wäre. das gilt dann bis zur neuberechnung für alle vertex vektorfelder."
5. "lässt sich das performant implementieren? kann sowas auch für edges und faces funktionieren?" (density-driven elimination order)
6. "ja, deine empfehlungen und ja auf allen 3 domänen. lässt sich farthest "point" auch für edges und faces berechnen?"

## Decisions (operator, 2026-10-22)
- Domains: mesh vertices, graph nodes, point-cloud points, mesh/graph edges, mesh faces.
- Slider: absolute arrow count (logarithmic); every-n-th stays only as the fallback without a valid order.
- Default method: the most uniform one within the time budget; the other methods stay selectable.
- The order is stored as a per-domain rank property (saved with the scene, visible in Properties).
- A stale order (cardinality/topology change) shows a hint and falls back to every-n-th; no automatic recompute.
- Edges and faces use their anchors (edge midpoints, face centers), as the renderer already does.

## Acceptance criteria
- [ ] Comparison harness, packet and two PASS pre-flight reviews (fresh Claude, fresh Codex) before any results-bearing run.
- [ ] Comparison run, HTML report from raw outputs, referee audit, method decision recorded here.
- [ ] Button computes the order per entity domain (job, undoable rank publication); status ready / missing / stale.
- [ ] Vector-field layers on a domain with a valid order draw the first N rows of it (slider); fallback stride otherwise.
- [ ] Tests fail without the change (order prefix rows, stale fallback, UI slider/button path); docs updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(SandboxDomainPanels|VisualizationRecipes|RuntimeRenderExtraction)\.'
python3 tools/agents/check_task_policy.py --root . --strict
```
