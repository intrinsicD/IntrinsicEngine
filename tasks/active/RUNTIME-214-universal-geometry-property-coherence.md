---
id: RUNTIME-214
theme: B
depends_on: [RUNTIME-195, RUNTIME-197, BUG-132]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-05T12:33:20Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
  - geometry.element-domain-sources
  - geometry.property-coherence
  - method.engine-integration
---
# RUNTIME-214 — Universal geometry-property CPU/GPU coherence

## Goal

- Make canonical geometry-property mutations observable without method-specific
  renderer dirty calls, so CPU methods publish to CPU properties and the next
  rendering extraction uploads the changed channels automatically, while GPU
  methods upload their input boundary once, remain GPU-local internally, and
  publish one terminal readback to the canonical CPU property boundary.

## Non-goals

- No second GPU allocator, residency service, transfer queue, property
  container, or renderer-owned ECS state.
- No per-iteration GPU readback, ordinary-path device-wide idle wait,
  permanently mapped device-local memory, or direct Vulkan type in
  geometry/runtime APIs. The existing synchronous write remains only the
  fail-safe when bounded staging rejects an upload.
- No requirement that a method compute directly in the renderer-owned packed
  geometry allocation; method-private working buffers remain valid until a
  measured zero-copy contract justifies shared compute/render residency.
- No change to method mathematics, support-radius policy, typed property-domain
  eligibility, topology/cardinality ownership, or UI/config controls.

## Context

- Geometry property storage currently exposes mutable handles, spans, vectors,
  and element access without a content revision. Runtime rendering therefore
  observes only explicit ECS dirty tags; a method that correctly publishes a
  property can leave an already-resident GPU geometry record unchanged.
- `GpuWorld` and `GeometryResidencyCoordinator` already own packed rendering
  allocation, partial uploads, barriers, and deferred retirement. GPU method
  implementations already own working-buffer upload/readback lifetimes through
  `Graphics.GpuTransfer`. The missing piece is a universal CPU property-content
  signal and a boundary rule joining those existing owners.
- Mutable access must conservatively count as a possible write because public
  spans and references cannot intercept later assignment. Const access must
  remain side-effect free. Explicit dirty tags remain useful precision hints,
  but correctness must not depend on every method knowing renderer internals.

## Control surfaces

- Config: none; coherence is an invariant, not a tuning option.
- UI: no private UI path; every UI-triggered method uses the same method
  publication and extraction boundaries.
- Agent/CLI: no private command path; every runtime/config-triggered method uses
  the same boundaries.

## Backends

- Backend axis: CPU methods mutate canonical CPU properties; Vulkan methods
  upload canonical inputs before dispatch, keep intermediate iterations on the
  GPU, and read back only terminal published outputs. Null/non-operational GPU
  requests retain their existing fail-closed/fallback behavior.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Any compatible typed property/span on a resolved vertex, halfedge, edge, or face domain; the coherence signal belongs to property storage, not a container or provenance. |
| Compatible entity sources | Point cloud, graph, and mesh sources according to the canonical element-domain matrix; mesh face/edge/halfedge properties remain valid point sets where method contracts allow them. |
| RuntimeModule | Existing method owners retain dispatch/publication; `RenderExtractionCache` compares property revisions and submits copied update plans to the existing `GeometryResidencyCoordinator`. |
| Config/agent | Existing method config and agent/CLI request paths are unchanged and inherit coherence automatically. |
| UI | Existing canonical preflight/actions are unchanged and inherit coherence automatically; no renderer-specific UI mutation path is added. |
| Publication | Same-cardinality CPU and terminal GPU results update only the named output properties on their originating domains; topology/cardinality replacement remains explicit and existing ECS dirty tags remain precision hints. |
| End-to-end tests | CPU property mutation without dirty tags must update resident rendering bytes; a representative Vulkan LOP method must publish terminal CPU output and update rendering with validation clean. |

## Right-sizing decision

- Add one monotonic revision clock to the existing property registry/storage
  family and one runtime-private observed-revision snapshot in existing
  extraction sidecars. Reuse `GeometryResidencyCoordinator`, `GpuWorld`, and
  `Graphics.GpuTransfer`; introduce no new service, interface, factory, or
  per-method adapter.
- Conservative mutable-access invalidation is preferred over wrapper/proxy
  reference machinery: it may schedule an unnecessary update when a caller
  borrows mutable storage only to read it, but it cannot miss an actual write
  and keeps contiguous property access intact.

## Required changes

- [x] Add monotonic content revisions to every geometry property registry and
      typed storage, with const observation side-effect free and mutable access
      conservatively marking the storage modified.
- [x] Expose revision observation through existing property handles,
      descriptors, and property sets without adding a parallel property API.
- [x] Cache source revisions in runtime extraction sidecars and merge revision
      deltas with explicit ECS dirty hints before building existing partial or
      full `GeometryUploadPlan` updates for mesh, graph, point-cloud, and mesh
      primitive-view lanes.
- [x] Preserve the method boundary contract: CPU methods publish canonical CPU
      properties; GPU methods upload once before dispatch, use GPU-local
      intermediates, then complete terminal readback/publication before their
      result is reported as applied.
- [x] Route device-local inputs for every current Vulkan geometry method and
      packed rendering updates through one shared staging-first upload helper;
      keep direct mapped writes for deliberately host-visible buffers and the
      synchronous device write only as a rejected-staging fallback.
- [x] Keep explicit ECS dirty tags as precise hints and compatibility signals,
      while making property-content correctness independent of those tags.
- [x] Define the reusable property-coherence contract in the architecture
      catalog with executable CPU and Vulkan proofs.

## Tests

- [x] Property contracts prove monotonic mutation revisions, no revision change
      through const access, copy/move behavior, structural mutation, and erased
      descriptor observation.
- [x] Runtime extraction contracts prove a resident mesh, graph, and point
      cloud reupload changed position bytes after direct property mutation with
      no ECS dirty tag, while unchanged sources do not reupload.
- [x] Partial-update contracts prove position-only mutation does not force
      topology or unrelated channel replacement.
- [x] Shared transfer contracts prove accepted staging avoids the synchronous
      device write and rejected staging takes the explicit fallback.
- [x] The validation-enabled Vulkan LOP workflow proves terminal CPU
      publication and subsequent render-residency change with zero validation
      errors.

## Docs

- [x] Document canonical CPU ownership, property revisions, method dispatch and
      completion boundaries, render extraction, staging, barriers, and deferred
      retirement in geometry/runtime/graphics architecture docs.
- [x] Refresh the contract catalog, task indexes/session brief, and module
      inventory if required.
- [ ] Bind the high-risk completion report and durable handoff to the final
      implementation revision, then obtain an accepted independent fixed-surface
      review before retirement.

## Acceptance criteria

- [x] Any existing or future CPU method that mutates a canonical geometry
      property through the public mutable API automatically makes that content
      eligible for upload on the next extraction without renderer knowledge.
- [x] Any GPU method reports applied success only after terminal GPU output is
      published back to canonical CPU properties; rendering observes that same
      publication without an extra method-specific upload call.
- [x] GPU method iterations do not introduce intermediate CPU copies, and
      unchanged resident geometry does not upload every frame.
- [x] Mesh, graph, point-cloud, and compatible non-vertex property domains keep
      their existing method eligibility/publication semantics and ownership
      boundaries.
- [x] Focused CPU, sanitizer, and promoted Vulkan validation gates pass on the
      frozen implementation surface.

## Completion status

- The implementation, CPU/full, ASan, UBSan, structural, and
  validation-enabled promoted-Vulkan gates are complete. The final Vulkan
  receipt covers the staging overwrite smoke plus LOP parity and child-mesh
  publication-to-rendering behavior.
- Two earlier Vulkan receipts are intentionally retained as causal evidence:
  the first exposed missing destination overwrite ordering, and the second
  exposed invalid queue/present setup in the new smoke harness. The final
  receipt passes after both defects were corrected.
- Retirement remains pending only the profile-required independent
  fixed-surface review. The implementation driver cannot self-accept that
  record.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Properties|GeometryExtraction|PointCloudConsolidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'Geometry|PointCloudConsolidation' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root . --require-complete RUNTIME-214
```

## Forbidden changes

- Adding a method-specific renderer upload call, a duplicate residency owner,
  or live ECS knowledge below runtime.
- Treating a bounding-box fingerprint or caller-maintained dirty tag as the
  sole correctness signal for mutable property content.
- Copying GPU intermediate results to CPU between iterations or reporting
  method success before terminal CPU publication completes.
- Mixing unrelated method numerical, UI, or renderer recipe changes into this
  contract slice.

## Maturity

- Target: `Operational` for the representative promoted Vulkan method/render
  path and `CPUContracted` for the backend-neutral universal property boundary.
