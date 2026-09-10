---
id: RUNTIME-224
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-09T23:47:37Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-224 — Bilateral point filtering with shared spatial backends

## Goal
Expose the existing fixed-normal bilateral point filter through canonical property/config/UI controls, with reference octree, CPU LBVH and framed Vulkan neighborhoods rebuilt for each moving-position pass.

## Non-goals
- No claim of complete Fleishman mesh denoising or Framework24 face-normal filtering/reconstruction parity.
- No topology/cardinality changes, new scheduler or spatial service, or default GPU selection.

## Context
The operator authorized sequential LBVH integrations then Framework24 ports until 07:00 Europe/Berlin on 2026-09-10. RUNTIME-223 is the completed starting checkpoint. Moving-point working sets will serve subsequent iterative point methods without publishing intermediate geometry.

## Formulation
Preserve the existing point-set kernel: fixed input normals, simultaneous updates along each normalized normal, spatial Gaussian times absolute-normal-dot Gaussian, min(n,k+1) candidates ordered by squared distance/source ID then self removal. Resolve auto spatial sigma once as twice sampled nearest-other spacing (up to 500 stride samples); zero spacing uses explicit 0.01 fallback. Each pass rebuilds neighborhoods. Zero iterations copies input. Finite input/parameters/results are required; degenerate normals retain their positions; failure never partially mutates the cloud.

Reviewed [Fleishman, Drori and Cohen-Or 2003](https://www.cs.tau.ac.il/~dcor/articles/2003/Bilateral-Mesh-Denoising.pdf), whose offset-similarity kernel differs from this existing normal-dot filter; [Zheng et al. 2011](https://doi.org/10.1109/TVCG.2010.264) describes face-normal filtering and reconstruction. Framework24 `bcg_mesh_normal_filtering.cpp` implements multiple mesh-normal variants, a separate convergence obligation.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Count-matched finite position and normal float3 spans, optional supplied neighbor rows. |
| Compatible entity sources | All eight canonical domains with compatible properties and deletion masks. |
| RuntimeModule | Existing geometry operations, SpatialIndexCache working-set leases and JobService dependencies. |
| Config/agent | sandbox.bilateral_filter persisted typed bindings, parameters, backend and batch size. |
| UI | Bilateral Point Filter panel and provenance menus use the same validate/apply path. |
| Publication | Named same-domain vec3 positions; explicit in-place output, atomic revision-checked undo/redo, preserve unrelated properties/topology. |
| End-to-end tests | Reference/CPU LBVH/all-domain history; actual Vulkan multi-pass, stale/cancel and private-index lifetime. |

## Spatial acceleration review
First-pass immutable source may reuse the entity index; later passes own temporary index snapshots through the existing cache, with identity compact slots. Temporary entries expire when the caller releases its lease; pending GPU work retains resources to safe completion. Original entity revisions still guard final publication. Vulkan k<=63 and existing coordinate/count/batch limits fail closed. Normals stay fixed and no stale neighborhood is reused after moving positions.

## Right-sizing
Add an owned workspace record and creation function to the existing cache rather than a parallel spatial service. Reuse geometry neighbor validation and single-pass reduction, property-domain resolution, existing jobs, config control and history. These are needed by a present moving-point consumer and subsequent iterative consumers.

## Slice plan
One bounded slice: reference/span contract and analytic tests, manifest, private cache workspaces, runtime/config/UI, CPU/Vulkan verification, docs and terminal independent review.

## Required changes
- [x] Add atomic span/filter-step geometry APIs and private spatial workspaces.
- [x] Wire same-domain output, in-place history and all control surfaces.
- [x] Rebuild neighborhoods each iteration and preserve input authority through completion.

## Tests
- [x] Analytic, invalid-input, simultaneous update and multi-pass tests pass.
- [x] All-domain CPU/cache, history, stale/cancel and config tests pass.
- [x] Actual Vulkan multi-pass comparison and manifest smoke pass.

## Docs
- [x] Update architecture, spatial consumers, method records, module inventory and follow-up reminders.

## Acceptance criteria
- [x] All three selectable backends complete the same bounded filter workflow.
- [x] No intermediate positions reach ECS; final publication and undo preserve property coherence.
- [ ] Full CPU gate, focused actual Vulkan and independent final-surface review pass with explicit limitations.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.Bilateral' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
```

## Forbidden changes
No layer exceptions, silent backend fallbacks, hidden topology edits or unsupported speedup/paper-equivalence claims.

## Verification constraints
BUG-178 requires CCACHE_DISABLE=1; existing BUG-180 excludes GPU LSan while retaining ASan+UBSan. Distinguish execution from capability skips.

## Verification observations
The full CPU run initially found an obsolete menu-count expectation (56 versus 60 after four new menu aliases). Correcting the expectation yields 4,424 selected tests and zero failures. Five native-window follow-ups cover sandbox skips; one unsanitized leak-control skip remains expected.

The first actual Vulkan run exhausted the 95-second fixture budget in phase 3. A diagnostic repeat measured phase 3 at only 1,991.63 ms: successful phases 0–2 consumed the budget. A separate instrumented run completed all six phases in 57.32 seconds with zero position error. The fixture now uses 128 query rows per batch, preserving every domain/pass/k=63/history/stale/cancel/dense assertion while issuing one partial batch per 65-live-row domain. Limits remain 95 seconds internally and 120 seconds in CTest. Historical failed attempts and the diagnosis are retained with final evidence.

Both 128-row repetitions passed (including all six phases), but one still approached the global timeout. Final registration separates comparison phases from stale/cancel/dense phases into two independently bounded Vulkan tests. All assertions and the original per-test limits remain.

The first split run passed the comparison case (60.77 seconds), but the new cancellation case inherited CTest's 30-second default. Its explicit registration now shares the established 120-second Vulkan limit; the internal watchdog remains 95 seconds.
