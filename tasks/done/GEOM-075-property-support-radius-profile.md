---
id: GEOM-075
theme: I
depends_on: [METHOD-016, METHOD-017, METHOD-018]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-LOPVulkan"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-04T23:15:36Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.support-radius-policy, method.engine-integration]
contract_review: "GEOM-075 establishes geometry.support-radius-policy through the canonical architecture document, catalog entry, Geometry.SupportRadius unit proof, and the existing property-aware consolidation runtime/config/UI path; element-domain substitutability and publication semantics remain governed by their existing contracts."
maturity_target: Operational
completed_on: 2026-08-05
---
# GEOM-075 — Property-specific support-radius profile and automatic LOP policy

## Status
- Completed on 2026-08-05 at `Operational`. The geometry-owned estimator,
  automatic/manual runtime policy, workload guard, config/agent/UI controls,
  telemetry, documentation, and property-domain tests are complete. The full
  CPU gate selected 4,087 cases with zero failures and one policy-defined
  GLFW/LSan environment skip; isolated ASan selected 2,673 with zero failures,
  and UBSan selected 2,673 with zero failures plus the same skip. Strict
  structural checks and the final fixed-surface evidence bundle close this
  revision.
- Commit: pending final evidence binding.

## Goal
- Deliver one geometry-owned, entity-independent neighborhood profile for finite
  `vec3` samples, then use it through the existing point-set consolidation
  runtime/config/UI path to resolve a bounded automatic support radius for every
  LOP-family strategy before CPU or Vulkan execution.

## Non-goals
- No CUDA, Vulkan shader, GPU buffer, or backend-selection work; METHOD-020 owns
  Vulkan execution after this task resolves a world-unit radius.
- No authored ECS `support_radius` component and no cache without a trustworthy
  property-data revision key.
- No adaptive per-point radius, approximate-neighbor method variant, or change
  to the frozen LOP/WLOP/CLOP/EAR equations.
- No generic spatial-index interface, service, registry, or method framework.

## Context
- Owner/layer: the reusable profile and recommendation records live in
  `src/geometry` (`geometry -> core`); runtime owns property binding, Auto versus
  Manual intent, workload preview, and the explicit resolved radius passed to
  `Geometry.PointCloud.Consolidation`.
- Architecture decisions A30/A31 require deterministic bounded k-nearest
  profiling over the selected property, method policy mapping, bounding-box
  scale as diagnostics rather than the primary estimate, and recomputation until
  a property revision contract can safely key a cache.
- The current engine-exposed methods with a world-unit support radius are the
  LOP, WLOP, CLOP, anisotropic WLOP, and EAR strategies behind the one
  point-cloud-consolidation operation. `Geometry.ImplicitPlaneField` owns a
  separate hierarchy-local radius rule and SPH owns its physical smoothing
  length; neither is silently rebound to this point-set recommendation.
- Right-sizing: add one deep plain-record/free-function geometry module and
  extend the existing runtime operation. Do not add a second queue, service,
  cache component, or polymorphic estimator. Reconsider caching only when a
  stable property-data revision exists.

## Control surfaces
- Config: schema-versioned `sandbox.point_cloud_consolidation` persists
  `support_radius_mode: auto|manual`, the manual world-unit value, and bounded
  workload limits.
- UI: the existing Sandbox point-set consolidation panel selects Auto/Manual,
  displays the resolved recommendation/work estimate, and submits through the
  existing validated apply path.
- Agent/CLI: the same config codec and typed request record drive resolution;
  there is no UI-only estimator state.

## Backends
- Backend axis: the CPU reference remains the only executing backend in this
  task. Both CPU reference and METHOD-020 Vulkan receive the same resolved
  positive world-unit radius and workload decision.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | A finite contiguous `vec3` property/span; no point-cloud container or topology is required. |
| Compatible entity sources | Any resolved vertex, halfedge, edge, or face property on point-cloud, graph, or mesh provenance that satisfies the existing point-set preflight. |
| `RuntimeModule` | Extend `Extrinsic.Runtime.PointCloudConsolidationModule`; resolve Auto from the captured selected property on the existing job worker before method execution and pass only the resolved radius to geometry. |
| Config/agent | Version and round-trip Auto/Manual intent plus bounded work limits through `Runtime.PointCloudConsolidationConfig` and the canonical config-section validator. |
| UI | Extend the existing property-aware consolidation panel for every compatible domain; use the same config preview/validate/submit path. |
| Publication | Unchanged: same-cardinality results update named properties on the originating domain; only topology-free point-cloud replacement may change cardinality. The derived profile is not published as authored geometry data. |
| End-to-end tests | Cover mesh/graph/point-cloud and non-vertex property bindings through the existing runtime contract/integration suites, plus config-source parity and panel submission. |

## Required changes
- [x] Add a focused `Geometry.SupportRadius` module with plain params/result
      records and free functions that build one KD-tree, deterministically sample
      at most 2,048 finite positions, collect self-excluding k-distance rank
      distributions, robust quantiles, bounding-box diagnostics, and explicit
      degeneracy/build statuses.
- [x] Add method-policy mapping for the current LOP-family strategies and return
      a positive world-unit recommendation plus sampled P50/P95/max support
      occupancy and predicted contribution diagnostics. Exact radius membership
      remains a KD-tree broad phase followed by distance cutoff.
- [x] Extend the point-set consolidation config schema with explicit Auto versus
      Manual intent and bounded neighbor/contribution limits; serialize,
      validate, and round-trip both config-file and typed control paths.
- [x] Resolve Auto from the selected property snapshot on the existing worker,
      reject degenerate or over-budget requests with actionable diagnostics, and
      carry requested/resolved radius mode, radius, profile, and workload fields
      into completion telemetry.
- [x] Extend the existing Sandbox panel and result summary without bypassing the
      validated config/operation path.
- [x] Make METHOD-020 depend on this task and consume only the explicit resolved
      radius; do not introduce GPU behavior in this task.

## Tests
- [x] Add deterministic geometry unit tests for uniform, non-uniform, duplicate,
      degenerate, non-finite, sample-capped, quantile, policy, exact-cutoff, and
      workload-limit cases.
- [x] Update config codec/integration tests for schema migration, Auto/Manual
      round-trip, invalid limits, and Editor/AgentCli/Programmatic parity.
- [x] Update runtime point-set consolidation tests across point-cloud, mesh,
      graph, face/edge/halfedge property domains to prove the selected property
      drives the recommendation and Manual remains exact.
- [x] Update Sandbox panel integration tests to prove Auto intent is submitted
      through the existing command path and resolved diagnostics return.

## Docs
- [x] Document the estimator, initial method policies, complexity, degeneracy,
      workload limits, and the no-cache-without-revision rule in geometry,
      method-package, runtime/config, and UI-facing current-state docs.
- [x] Regenerate the C++23 module inventory and task session brief.

## Acceptance criteria
- [x] Every current runtime LOP-family strategy defaults to Auto and receives a
      deterministic property-specific positive radius or fails before method execution
      with an actionable profile/workload status.
- [x] Manual mode passes the authored world-unit radius unchanged through all
      config/agent/UI sources.
- [x] The bounded profile samples no more than 2,048 points, reports exact
      sample/neighbor diagnostics, and never uses bounding-box diagonal as its
      primary recommendation when valid k-distance data exists.
- [x] Workload preview reports sampled P50/P95/max occupancy and rejects the
      configured unsafe contribution/memory envelope before method execution.
- [x] Property-domain substitutability and existing publication/cardinality,
      undo/redo, stale-source, and renderer-dirty behavior remain intact.
- [x] Default CPU gates, strict structure/docs checks, and independent
      fixed-surface review pass for the final high-risk revision.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SupportRadius|PointCloudConsolidation|SandboxConfigSections|SandboxPointCloudConsolidationPanel' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
```

## Forbidden changes
- No CUDA dependency, token, implementation, or compatibility alias.
- No silent fallback from an invalid/over-budget Auto recommendation to the
  old `1.0` world-unit default.
- No entity-provenance or vertex-wrapper restriction on compatible `vec3`
  properties.
- No implicit replacement of mesh/graph topology or unrelated properties.
- No cache keyed only by entity identity or property name.
- No performance or quality claim from the policy defaults without a declared
  benchmark baseline.

## Maturity
- Reached: `Operational` through the real CPU-reference runtime/config/UI path.
- Vulkan `Operational` and cross-backend parity remain owned by METHOD-020.

Completed: 2026-08-05. Final source revision, content digest, durable handoff,
and independent acceptance are recorded under `tasks/evidence/GEOM-075/`.
