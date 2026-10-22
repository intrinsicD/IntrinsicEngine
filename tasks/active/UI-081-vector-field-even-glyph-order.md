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
- Default method: farthest point (see Log); the other methods stay selectable in the Appearance combo.
- The order is stored as a per-domain rank property (saved with the scene, visible in Properties).
- A stale order (cardinality/topology change) shows a hint and falls back to every-n-th; no automatic recompute.
- Edges and faces use their anchors (edge midpoints, face centers), as the renderer already does.

## Acceptance criteria
- [ ] Mesh comparison of the ordering methods as an exploratory report (operator decision 2026-10-22:
      report only, light pre-flight tier, no decision rests on it); HTML report from raw outputs.
- [x] Default method: farthest point, by operator decision 2026-10-22 on claim C116 (best coverage of
      the compared methods on point clouds); the comparison does not set it.
- [ ] Button computes the order per entity domain (undoable rank publication); status ready / missing / stale.
- [ ] Vector-field layers on a domain with a valid order draw the first N rows of it (slider); fallback stride otherwise.
- [ ] Tests fail without the change (order prefix rows, stale fallback, UI slider/button path); docs updated.

## Log
- 2026-10-22: two pre-flight rounds on the comparison (v1 digest 6601e5b5: Codex BLOCK, Claude REVISE;
  v2 digest ee491b0a: Codex BLOCK, Claude REVISE) found real harness defects (seed wiring, gates,
  inventory, per-call deadlines). The operator then chose farthest point as the default on C116 and
  the comparison as a report only. Packets and verbatim reviews: tasks/evidence/UI-081/preflight/.
- 2026-10-22: feature code review, Claude → fresh Claude (claude-opus-5-5, effort max, reported
  claude-opus-5-5), session 9b81f9d1-9c44-46e6-8ad2-98fdf289b905, object HEAD 1f6bd5a6f + the
  feature diff and new TU, digest 560545166b34323b7714b968326681165f5f26f1e7e14b9592c91928fb2bd42c,
  verdict "revise required" (R1 non-float rank crash, R2 O(N) model scan per frame, S1 canonical vs
  displayed positions, S2 old Stride>1 layers jump to all arrows, minor: 2^24 domains, slider
  max, stale message, docs). All addressed in the feature commit; recursion protection enforced
  (Read/Grep/Glob only). Packet and answer: tasks/evidence/UI-081/code-review/.
- Open: Geometry.PointSampling sample elimination derives its weight limit dMin once from k/n
  (Geometry.PointSampling.Approximate.cpp:269), so it is 0 for every progressive stage at count = N;
  check against the cyCodeBase reference before relying on it.

## Verification
```bash
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(SandboxDomainPanels|PointSamplingOperations|VisualizationRecipes|RuntimeVectorFieldExtraction|VisualizationOverlayPass)\.'
python3 tools/agents/check_task_policy.py --root . --strict
```
