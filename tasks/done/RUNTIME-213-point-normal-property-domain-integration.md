---
id: RUNTIME-213
theme: I
depends_on: [GEOM-026, HARDEN-087]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive implementation and publication; evidence is the reviewed diff, tests and existing method records."
owner: codex-interactive
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-08T18:58:52+00:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-213 — Point-set normal property-domain integration

## Goal

- Expose the geometry point-set normal estimator for any selected finite
  `vec3` property on any resolved element domain, while retaining the distinct
  mesh- and graph-topology normal methods where topology is genuinely required.

## Non-goals

- No replacement of mesh face-weighted or graph connectivity-aware normal
  kernels, learned estimator, or numerical-policy change.
- No converter or requirement that a point-set sample be a vertex.
- No UI implementation; `UI-045` owns method/property selection.

## Context

- Before this integration, the editor advertised normals only for mesh vertices,
  graph nodes and point-cloud points. `Geometry.PointCloud.Normals::Estimate` already has a
  span contract, so face centers and other typed sample properties are valid
  point-set inputs even though topology-aware alternatives remain distinct.
- Re-read Hoppe et al.'s PCA-plane normal/orientation construction (DOI
  `10.1145/133994.134011`) and Mitra–Nguyen's noise/curvature/density-aware
  neighborhood analysis before implementation. Record later robust/learned
  estimators as excluded variants rather than changing the landed oracle.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point-set variant: finite `Property<vec3>`; mesh/graph variants: their explicit topology plus named properties. |
| Compatible entity sources | Point-set estimator on every element domain; topology-aware mesh/graph methods only where their named adjacency exists. |
| RuntimeModule | Extend the existing normal command/job/history path and availability model. |
| Config/agent | One validated method selection and parameter path, including property refs. |
| UI | `UI-045` distinguishes point-set from topology-aware methods and lists compatible properties. |
| Publication | Named same-cardinality normal property on the originating domain; no topology or input mutation. |
| End-to-end tests | Property-domain point estimator, topology-aware method gating, staleness/history/config/UI parity. |

## Spatial acceleration consideration

Consider reusing a canonical position-property index for point-set PCA
neighborhoods. Radius queries require complete-hit handling; kNN and nearest-other
searches now have shared kNN/exclusion support (GEOM-077). A supplied CPU LBVH PCA overload preserves the current estimator; wire its selection and publication here. Preserve
topology-based mesh/graph normal variants and current numerical policy; any index
adaptation remains explicitly scoped.

See the [shared spatial-index consumer inventory](../../docs/architecture/spatial-index-consumers.md).

## Required changes

- [x] Model normal-method requirements explicitly instead of deriving them
      from entity provenance or a `VertexProperty` type.
- [x] Resolve point-set input/output refs on every element domain and reuse the
      canonical availability/property catalog.
- [x] Keep mesh/graph topology-aware paths and their diagnostics distinct while
      routing all publication through one named-property transaction shape.

## Tests

- [x] Run the point-set estimator over each physical property-domain family,
      including face centers, and compare identical inputs.
- [x] Prove topology-aware variants require only their documented adjacency,
      preserve unrelated/custom properties, and publish/undo/redo exactly.
- [x] Cover config parity and stale input/output property rejection.

## Docs

- [x] Update normal method/runtime docs with the explicit method-to-input
      matrix and reviewed original/extensions literature.

## Acceptance criteria

- [x] Point-set normal estimation never requires point-cloud provenance or a
      vertex property.
- [x] Stronger mesh/graph algorithms remain available only by their real
      topology contract, not by menu identity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Normal' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No method conflation, converter, provenance-only gate, handle-specific point
  estimator, or unreviewed numerical substitution.

## Interactive implementation scope
The operator accepted the normal-estimation continuation after GEOM-077. RUNTIME-213 and UI-045 are one user-visible workflow: canonical input/output refs, persisted normal method/config, point PCA with CPU KD-tree or cached CPU LBVH, distinct mesh/graph topology methods, copied CPU jobs, stale-safe named-property history and one shared UI window. No GPU normal solve or default change. Extend the existing operations owner and config schema pattern; no new service or scheduler. Legacy default-property command APIs remain compatible.

## Implementation note

The shared configuration, typed normal command, copied job/publication/history path and single panel are implemented. See [normal-estimation contract](../../docs/architecture/normal-estimation.md). The CPU and editor checks below verify this implementation; GPU normal estimation and full Framework24 workflow parity remain outside this slice.

## Verification and review — 2026-09-08

Implemented and CPU-verified; see the completion record below.

- Configured `cmake --preset ci`; enabled `INTRINSIC_BUILD_SANDBOX=ON` for the app build. Clang 23, Debug, unsanitized, CUDA off; promoted Vulkan remains off.
- Built `IntrinsicTests`, `IntrinsicRuntimeContractTests`, `IntrinsicSandboxEditorIntegrationTests` and `ExtrinsicSandbox` with `CCACHE_DISABLE=1`.
- Focused CTest selector `Normal|SandboxEditorPresentation|DismissClearsOneGeometry`, excluding `gpu|vulkan|slow|flaky-quarantine`: 179/179 passed (3.62 s).
- Full exclusion-only CPU selector with timeout 60: 4,364 selected, 4,363 passed, the expected unsanitized `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` skip, zero failures (132.33 s).
- Strict layering, task policy, doc links, test layout, method/benchmark manifests and skill mirrors passed. Selected source-documentation audit has no objective errors; its three review findings concern the existing large operations interface. Root hygiene retains the pre-existing `.agents/` warning tracked by BUG-177.
- The first focused failure was a missing visualization capability flag in the new test fixture. Full-suite failures pinned the previous separate-panel command/reset names and window count. Those fixtures now exercise the shared window, including drawing it; the final runs above are the current results.

Scope/layering/tests/docs sweep: pass. The existing geometry-processing owner holds normal input capture, jobs and publication; the existing cache owns index leases. The only new module is a persisted config surface justified by real method/backend controls and UI/agent callers. No new service, scheduler, event bus, graphics ownership or lower-to-higher dependency was added. Readiness avoids mesh/graph snapshot construction; workers consume owned data. History retains only output states and consumed-property guards, and unrelated edits remain valid. Legacy normal command APIs remain available.

Clean-workshop rows: 1 imports pass; 2 target links pass; 3 exported-type direction pass; 4 renderer growth n/a; 5 new passes n/a; 6 recipe dependency changes n/a (uses the existing vector recipe); 7 scaffold retirement n/a (no retirement in this session); 8 temporary architecture exceptions n/a.

Evidence scope is CPU computation/publication and editor integration. No new GPU normal solve, GPU visualization readback, sanitizer result, performance improvement or complete Framework24 PCA/feature/saliency parity is claimed.

## Completion — 2026-09-09

- PR/commit: `3276c70597761b089bea0364e3fad7eb091fa869`

Completed in implementation commit `3276c70597761b089bea0364e3fad7eb091fa869`. Operational CPU normal computation/publication and editor integration, verified by the normal/domain/history/config tests and full CPU suite. PCA/MST remain CPU computations; no GPU normal solve or complete Framework24 workflow parity is claimed.

Publication verification on the combined source: Clang 23 `ci` configured; `IntrinsicTests` and `ExtrinsicSandbox` built. The full CPU selector passed 4,363 tests with one expected unsanitized GLFW/LSan skip and zero failures (115.50 s). Strict layering, task policy, doc links, test layout, manifests and skill mirrors pass. GPU/sanitizer evidence above is from 2026-09-08; this publication check did not rerun those lanes.
