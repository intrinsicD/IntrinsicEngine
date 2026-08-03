# Clean-workshop review — RUNTIME-206 LOP property-domain sources

## Change under review

- Change: retire
  [`RUNTIME-206`](../../tasks/done/RUNTIME-206-lop-element-domain-source-integration.md)
  after replacing exact point-cloud provenance with one typed-property
  availability, job, publication, and history path across all geometry element
  domains.
- Trigger: changes a public runtime method-integration contract and its ECS
  publication/history wiring.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | The strict layer and clean-workshop scans are clean. The span-based kernels remain in geometry; ECS/property resolution and composition remain runtime-owned. |
| 2 | CMake target links match layer policy | pass | No target, source list, or `target_link_libraries(...)` edge changed. |
| 3 | No public API exposes a higher-layer type to a lower layer | pass | The new property references and availability value are runtime DTOs built from the existing runtime geometry catalog; no lower-layer API changed. |
| 4 | Renderer member/subsystem growth justified by an owning seam | n/a | No renderer state, subsystem, service, or graphics ownership changed. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass or renderer command route changed. |
| 6 | New frame-recipe dependencies resource-driven or justified | n/a | No recipe, resource declaration, or ordering edge changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | The real asynchronous `Engine::Run()` path reaches `Operational`; `UI-039` owns property-aware Mesh/Graph/PointCloud discovery and METHOD-020 retains any future GPU backend. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No converter, facade, allowlist row, warning-mode gate, or temporary exception was added. |

## Architecture checklist result

- Point-set eligibility is determined by a selected finite `vec3` property on
  a resolved element domain. Mesh faces, mesh/graph edges and halfedges,
  mesh/graph vertex-like domains, and point-cloud points all enter the same
  validation and CPU-reference worker.
- Same-cardinality publication stages only the named output properties on the
  originating `PropertySet`; unrelated properties, including erased custom
  values, and all topology components remain untouched. Cardinality-changing
  requests fail before submission on topology-bearing domains and retain the
  existing exact point-cloud replacement/undo contract.
- The exported availability value, direct request, queued worker, stale-source
  validation, result record, and editor history share the same property
  references. `UI-039` can therefore consume the runtime preflight without an
  app-owned eligibility switch or conversion path.

## Findings → follow-ups

- No clean-workshop findings. The wider method audit records every remaining
  property-domain restriction as `RUNTIME-207`, `RUNTIME-209`,
  `RUNTIME-211..213`, `UI-039..045`, or `GEOM-073..074`; those are bounded
  follow-ups rather than exceptions in this slice.
