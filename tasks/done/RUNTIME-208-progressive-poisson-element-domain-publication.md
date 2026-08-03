---
id: RUNTIME-208
theme: I
depends_on: [HARDEN-087]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-RUNTIME208"
branch: "runtime/runtime-208-element-domain-publication"
worktree: "/tmp/intrinsic-runtime208.qxtdIf"
claimed_at: "2026-08-02T22:05:49Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-208 — Progressive Poisson element-domain publication

## Status

- Completed and retired on 2026-08-03 at `Operational`. Progressive Poisson
  now consumes existing mesh, graph, or point-cloud `Vertices` through one
  direct/queued/config/backend path and publishes source-cardinality rank,
  accepted-level, splat-radius, and prefix-visibility attributes without
  sampling or replacing the source domain. Mesh/graph topology, ordering,
  provenance, presentation, and non-target properties survive apply/undo/redo.
  Focused method/runtime/UI coverage passed 46/46. The default CPU selector
  selected 4,004 tests, passed 4,003, and skipped its policy-defined GLFW/LSan
  case; grouped ASan passed 2,656/2,656, while grouped UBSan selected 2,656,
  passed 2,655, and skipped the LSan-only case. Strict method-manifest,
  layering, task/docs, inventory, ARA, clean-workshop, and independent
  fixed-surface review evidence close the public-contract correction. No
  Vulkan compute-parity or performance claim is made; `METHOD-014` retains
  compute parity and `UI-038` owns the matching Graph panel.
- Commit: the implementation/evidence commits and accepted revision-bound
  review under `tasks/evidence/RUNTIME-208/` provide the exact source binding.

## Goal

- Align Progressive Poisson runtime integration with its point-span method
  contract: consume existing mesh, graph, or point-cloud `Vertices` directly
  and publish hierarchy attributes without surface resampling or entity-domain
  conversion.

## Non-goals

- No sampling-kernel or CPU/GPU parity change, new surface sampler, or generic
  geometry converter.
- No new Graph-panel registration; the re-scoped `UI-038` consumes this runtime
  surface while this task keeps the existing Mesh/PointCloud controls aligned.
- No deletion/reordering of topology-bearing vertex storage.

## Context

- `methods/geometry/progressive_poisson/method.yaml` declares any contiguous
  `vec3` property buffer. The current mesh path instead surface-samples faces
  and replaces the mesh with a point cloud, while graph sources are omitted.
- Review the repository's unpublished working draft and canonical package URL
  (`https://github.com/intrinsicD/GPU-Accelerated-Progressive-Poisson-Disk-Sampling-via-Phase-Parallel-Spatial-Hashing`)
  together with Brandt et al.'s visibility-aware progressive farthest-point
  sampler (DOI `10.1111/cgf.13848`) and Yuksel's progressive-capable weighted
  sample elimination (DOI `10.1111/cgf.12538`) before editing. The published
  works are comparative adjacent methods, not provenance for the repository's
  exact in-house formulation. Preserve the distinction between reordering or
  selecting an existing input set and generating samples on a continuous
  surface.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite contiguous vertex positions. |
| Compatible entity sources | Mesh vertices, graph nodes, and point-cloud points. |
| RuntimeModule | Refactor the existing Progressive Poisson typed operation/job path. |
| Config/agent | Keep one validated config lane; remove surface-generation knobs that do not belong to this method contract. |
| UI | `UI-038` exposes the same method under Mesh, Graph, and PointCloud. |
| Publication | Publish input-index rank/accepted-level/radius properties at source cardinality with documented sentinels; preserve topology and element order. |
| End-to-end tests | Three-domain runtime/config/publication and UI-discovery tests plus CPU/GPU/fallback parity already required by method tasks. |

## Required changes

- [x] Replace mesh/point-cloud provenance branching with canonical `Vertices`
      extraction shared by mesh, graph, and point-cloud entities.
- [x] Delete the implicit mesh-surface-sampling/domain-replacement branch from
      this operation and remove its method-mismatched config/UI fields through
      the existing schema migration/diagnostic policy.
- [x] Define source-cardinality properties for ordering, accepted level, and
      splat radius, including deterministic sentinel semantics for input points
      outside the accepted subset; bind visualization channels to those
      properties without reindexing source elements.
- [x] Publish through one history/generation-validated mutation that preserves
      mesh/graph topology and every non-target property; keep direct, queued,
      GPU, and fallback paths behaviorally identical.
- [x] Expose one copied availability/readiness result for all control surfaces
      and all three source domains.

## Tests

- [x] Run identical deterministic input positions from mesh, graph, and point
      cloud and compare method result buffers plus published properties.
- [x] Prove graph/mesh topology, vertex ordering, non-target properties, and
      provenance survive apply/undo/redo.
- [x] Prove queued publication and history fail closed on same-cardinality
      edits to the production typed mesh/graph connectivity properties.
- [x] Prove no mesh surface sampling or domain replacement occurs through Run,
      auto-run, config/agent, queued CPU, requested GPU, or fallback paths.
- [x] Cover config migration/removal diagnostics and all channel sentinel
      semantics.

## Docs

- [x] Update method, runtime, config, and Sandbox docs to distinguish point-set
      ordering from surface sample generation and document published channels.

## Acceptance criteria

- [x] Progressive Poisson is runnable from every `Vertices` source with no converter and with source-cardinality publication.
- [x] Mesh and graph topology/cardinality are unchanged on every backend path.
- [x] Obsolete surface-generation controls cannot trigger hidden conversion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No implicit surface sampling, topology/cardinality replacement, provenance
  rewrite, converter confirmation workflow, or backend-specific publication.
