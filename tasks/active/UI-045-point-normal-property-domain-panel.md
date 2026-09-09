---
id: UI-045
theme: I
depends_on: [RUNTIME-213]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: codex-interactive
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-08T18:58:53+00:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# UI-045 — Point-set normal property-domain panel

## Goal

- Let users choose the point-set normal estimator for any compatible position
  property while presenting topology-aware mesh/graph normal variants only
  when their real inputs resolve.

## Non-goals

- No normal kernel/runtime/config implementation, method conflation, converter,
  or per-domain panel copies.

## Context

- `RUNTIME-213` separates generic point-set eligibility from stronger
  topology-aware methods and records the Hoppe/Mitra–Nguyen literature basis.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point-set: selected finite `vec3`; topology-aware: explicit adjacency plus properties. |
| Compatible entity sources | Point-set on every element domain; stronger variants where their named topology resolves. |
| RuntimeModule | Consume `RUNTIME-213` method/property readiness and command results. |
| Config/agent | Edit the same validated method/config state. |
| UI | Shared method selector plus position/normal property selectors and exact requirements. |
| Publication | Visualize the named same-domain normal output. |
| End-to-end tests | Property discovery, method gating, config/run/history, diagnostics. |

## Spatial acceleration consideration

Let RUNTIME-213 own index acquisition and neighborhood capability checks. Expose
only actually implemented point-normal query/backend choices through shared
config, preserving the distinction between radius, kNN and topology-based normals;
a present LBVH service alone does not make the estimator accelerated.

See the [shared spatial-index consumer inventory](../../docs/architecture/spatial-index-consumers.md).

## Required changes

- [x] Group compatible position properties by element domain and filter normal
      outputs with the runtime preflight.
- [x] Explain point-set versus topology-aware neighborhood semantics and show
      exact missing-input reasons.
- [x] Submit only the typed runtime operation and preserve shared history and
      visualization paths.

## Tests

- [x] Cover face/edge/halfedge/vertex/node/point property discovery and
      point-set execution plus legitimate topology-aware disabled states.
- [x] Verify config parity and no provenance-only or handle-wrapper filter.

## Docs

- [x] Update Sandbox normal-method menus and property selection docs.

## Acceptance criteria

- [x] Generic point-set normal estimation is visible on every compatible
      property; stronger methods advertise only their actual topology needs.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'Normal' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No converter, provenance-only filtering, UI-owned mutation, duplicated
  method state, or unimplemented method advertisement.

## Interactive implementation scope
The operator accepted the normal-estimation continuation after GEOM-077. RUNTIME-213 and UI-045 are one user-visible workflow: canonical input/output refs, persisted normal method/config, point PCA with CPU KD-tree or cached CPU LBVH, distinct mesh/graph topology methods, copied CPU jobs, stale-safe named-property history and one shared UI window. No GPU normal solve or default change. Extend the existing operations owner and config schema pattern; no new service or scheduler. Legacy default-property command APIs remain compatible.

## Implementation note

The shared configuration, typed normal command, copied job/publication/history path and single panel are implemented. See [normal-estimation contract](../../docs/architecture/normal-estimation.md). The CPU and editor checks below verify this implementation; GPU normal estimation and full Framework24 workflow parity remain outside this slice.

## Verification and review — 2026-09-08

Implemented and CPU-verified in the shared working tree; awaiting commit/retirement.

- Configured `cmake --preset ci`; enabled `INTRINSIC_BUILD_SANDBOX=ON` for the app build. Clang 23, Debug, unsanitized, CUDA off; promoted Vulkan remains off.
- Built `IntrinsicTests`, `IntrinsicRuntimeContractTests`, `IntrinsicSandboxEditorIntegrationTests` and `ExtrinsicSandbox` with `CCACHE_DISABLE=1`.
- Focused CTest selector `Normal|SandboxEditorPresentation|DismissClearsOneGeometry`, excluding `gpu|vulkan|slow|flaky-quarantine`: 179/179 passed (3.62 s).
- Full exclusion-only CPU selector with timeout 60: 4,364 selected, 4,363 passed, the expected unsanitized `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` skip, zero failures (132.33 s).
- Strict layering, task policy, doc links, test layout, method/benchmark manifests and skill mirrors passed. Selected source-documentation audit has no objective errors; its three review findings concern the existing large operations interface. Root hygiene retains the pre-existing `.agents/` warning tracked by BUG-177.
- The first focused failure was a missing visualization capability flag in the new test fixture. Full-suite failures pinned the previous separate-panel command/reset names and window count. Those fixtures now exercise the shared window, including drawing it; the final runs above are the current results.

Scope/layering/tests/docs sweep: pass. The existing geometry-processing owner holds normal input capture, jobs and publication; the existing cache owns index leases. The only new module is a persisted config surface justified by real method/backend controls and UI/agent callers. No new service, scheduler, event bus, graphics ownership or lower-to-higher dependency was added. Readiness avoids mesh/graph snapshot construction; workers consume owned data. History retains only output states and consumed-property guards, and unrelated edits remain valid. Legacy normal command APIs remain available.

Clean-workshop rows: 1 imports pass; 2 target links pass; 3 exported-type direction pass; 4 renderer growth n/a; 5 new passes n/a; 6 recipe dependency changes n/a (uses the existing vector recipe); 7 scaffold retirement n/a (no retirement in this session); 8 temporary architecture exceptions n/a.

Evidence scope is CPU computation/publication and editor integration. No new GPU normal solve, GPU visualization readback, sanitizer result, performance improvement or complete Framework24 PCA/feature/saliency parity is claimed.
