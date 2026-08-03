# Clean-workshop review — RUNTIME-208 Progressive Poisson publication

## Change under review

- Change: retire
  [`RUNTIME-208`](../../tasks/done/RUNTIME-208-progressive-poisson-element-domain-publication.md)
  after replacing the mesh-surface/domain-conversion branch with one existing-
  `Vertices` operation and source-cardinality publication path for mesh, graph,
  and point-cloud entities.
- Trigger: changes a public runtime/config method-integration contract and its
  ECS publication/history wiring.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | The strict layer and clean-workshop scans are clean. Method code remains hermetic; ECS/config/UI composition remains runtime/app owned. |
| 2 | CMake target links match layer policy | pass | No target, source list, or `target_link_libraries(...)` edge changed. |
| 3 | No public API exposes a higher-layer type to a lower layer | pass | Only existing runtime DTOs changed; no lower-layer API or new dependency edge was introduced. |
| 4 | Renderer member/subsystem growth justified by an owning seam | n/a | No renderer state, subsystem, service, or graphics ownership changed. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass or renderer command route changed. |
| 6 | New frame-recipe dependencies resource-driven or justified | n/a | No recipe, resource declaration, or ordering edge changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | The CPU-backed runtime integration closes at `Operational`; `METHOD-014` remains the named Vulkan compute-parity owner and `UI-038` owns the Graph panel. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No shim, converter, allowlist row, warning-mode gate, or temporary exception was added. Obsolete config fields fail through the normal diagnostic policy. |

## Architecture checklist result

- Mesh, graph, and point-cloud provenance select the canonical vertex element
  domain but do not select distinct algorithms or publication code.
- One captured mutation validates and restores the complete topology-bearing
  geometry-source cohort while changing only Progressive Poisson vertex outputs
  and owned point/scalar visualization. Production graph/mesh connectivity
  records are value-compared; unrecognized erased property types fail closed
  instead of admitting a descriptor-only stale match.
- The config/agent, manual Run, debounced auto-run, queued CPU, and requested-
  GPU fallback routes enter the same typed operation; no conversion control or
  backend-specific publisher remains.

## Findings → follow-ups

- Independent fixed-surface round one found that erased production
  connectivity was compared by descriptor only. The corrected comparator now
  checks the typed vertex/halfedge/face connectivity values, the fixtures use
  real `PopulateFromMesh`/`PopulateFromGraph` sources, and queued mesh/graph plus
  history regressions prove same-cardinality edits are rejected without
  overwrite.
