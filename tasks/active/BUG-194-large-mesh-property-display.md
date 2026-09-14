---
id: BUG-194
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive user-reported display repair; source diff, regression tests and actual Vulkan diagnostics carry verification, with no performance claim.
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
  - method.engine-integration
---
# BUG-194 — Large-mesh property display and geodesic controls

## Goal
Fix the reported delayed Show saliency/mask behavior and uniformly pink geodesic
distances on dragon.obj. Use the common entity chooser in a geodesic control
layout consistent with normal estimation, initially following scene selection.

## Acceptance criteria
- [ ] Diagnose Show latency on the supplied dragon mesh and fix the confirmed shared cause.
- [x] Valid infinite geodesic distances no longer suppress the entire reachable field; preserve stored values and distinguish unreachable regions.
- [x] Scalar/mask publication reuses resident geometry; property changes and undo/redo still refresh visualization, and real position edits still update geometry.
- [x] Geodesic controls choose/follow an entity, keep vertex sources entity-local, and apply edited config through the common validated lane.
- [x] Regression tests, real Vulkan readback, focused sanitizers and full native CPU verification pass; remove temporary probes.

## Plan and reuse
Root is the only checkout writer. Claude performs bounded source-only diagnostic
and fixed-diff reviews. Reuse Runtime.VisualizationRecipes scalar encoding and
the shared shader colormap path for infinity handling; keep NaN/overflow rejection
and isoline finite-source requirements. Reuse DrawProcessingEntity and validated
GeodesicsConfig apply; no new engine service, compatibility path or algorithm.
The old geodesic domain wrapper already had an entity chooser; the dedicated
layout removes the unrelated domain summary and makes control ownership clear.
Investigate display waiting separately rather than attributing it to the solver.

## Engine integration
| Surface | Decision |
| --- | --- |
| Least-structured input | Existing triangle mesh, configurable vertex float3 positions and vertex source indices. |
| Compatible entity sources | Existing first-class triangle meshes; scalar display still supports every resolved scalar property domain. |
| RuntimeModule | Existing mesh-field commands, editor UI module and shared rendering extraction/upload owners. |
| Algorithm/backend | Existing CPU reference virtual-source method, unchanged. |
| Publication | Existing same-domain double distances and bool source mask, unchanged revisions/undo ownership; infinity means unreachable. |
| Config/agent | Shared validated config apply; entity selection stays local to the processing panel and follows selection changes. |
| Visualization | Shared scalar encoding excludes infinite samples from automatic range but preserves slots; unreachable fragments get a distinct gray color. |
| End-to-end tests | CPU method-to-recipe regression, live processing panel/config test, and promoted Vulkan scalar gradient/gray pixel readbacks. |
| Deferred integration | No new method/backend capability deferred by this repair. |

## Diagnosis ledger
Full commands, source-only reviews and append-only observations are archived locally in
`build/analysis/bug194-property-display-2026-09-14/` (ignored diagnostic artifacts).
The supplied OBJ contains 151,486 vertices, 302,938 triangle faces and seven
components (151,468 plus six components of three vertices). A main-component
seed necessarily leaves infinite distances on the six small components. The
new isolated-vertex regression fails before the fix: successful computation,
then NonFiniteValue and no scalar packet. A separate real-model Vulkan probe
checks first-frame scalar publication while import enrichment remains pending.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicShaderOutputs IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicShaderOutputs IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'AnalysisMaskSaliency|UnreachableVertexPreserves|UnreachableScalarRegion|DirectMeshEnrichmentPending|ReferenceTriangleScalarFieldSurfaceAndIsolines' --no-tests=error --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Review and verified results
Claude performed bounded source-only investigations and fixed-diff reviews;
root backtested each finding against source and executable regressions. Runtime
encoding and graphics upload validation both rejected legitimate infinite
geodesic distances. Both are repaired, with NaN/vector/color rejection retained.
The scalar shader returns neutral gray before LUT and inline isoline work;
independent isoline recipes still reject nonfinite source data.

A failing renderer integration regression exposed another shared defect:
keypoint publication forced one full mesh reupload; geodesic publication forced
one partial attribute reupload. Scalar-only transactions now rely on the existing
per-property revisions instead of unrelated geometry dirty tags. This covers
keypoints, outlier mask/score, geodesics and the shared scalar helper used by
kernel density, density weights and point spacing. Topology/cardinality and
vector/normal/UV update paths retain their existing handling. The regression
covers all four publication owners and a positive control for actual position
edits. Density-weight undo/redo tests now require property revision changes,
workspace notification and no unrelated geometry invalidation.

No new dependency edges, packet fields, services, passes or config fields were
introduced. The existing coherence contract already requires unbound properties
not to invalidate geometry consumers. Module inventory regeneration produced no
change. The source-doc scanner's declaration-comment review is intentional:
the public comment explains which numeric payloads the upload seam accepts.

| Final verification | Result |
| --- | --- |
| `ci` full `IntrinsicTests` + shader build and exclusion-only CPU CTest gate | 4,616 passed, one expected unsanitized GLFW/LSan skip, zero failures; 133.03s local run. |
| `ci-asan` focused publication/encoding/UI/render extraction | 117/117 passed, serial. |
| `ci-ubsan` same focused set | 117/117 passed, serial. |
| `ci-vulkan` five scalar/mask/Appearance/isoline/pending-enrichment readbacks | 5/5 passed; NVIDIA GeForce RTX 3050, driver 590.48.01; ASan+UBSan. |
| `dev` sandbox and shader build | Rebuilt `build/dev/bin/ExtrinsicSandbox` with promoted Vulkan and the preset's ASan+UBSan. |
| Structural checks | Strict layering, task policy, test layout, root hygiene, source documentation and doc links passed. |

## Remaining reproduction boundary
The exact desktop delay is not closed. The supplied dragon's synthetic scalar
reached the GPU on the next captured frame while an enrichment job remained
pending; this disproves an unconditional Show/import dependency, not every
possible frame stall. An actual dragon keypoint-panel probe with AsyncWorkModule
computed on the worker (about 14s in this local unsanitized Debug diagnostic),
kept its largest UI frame gap at 56ms, and applied both Show choices promptly
after publication. Its initial harness without AsyncWorkModule correctly used
the synchronous fallback and is not evidence of a production UI stall.

The confirmed unnecessary geometry reuploads are removed, but no matched real
model timing establishes that they explain the entire reported delay. These are
local diagnostic observations, not performance claims. The user's executable,
whether camera/UI remain responsive, and possible baked-texture mode still
matter if the symptom persists with the rebuilt developer sandbox. Keep this
note active until that remaining behavior is resolved; do not claim full
latency closure or retire it solely on the verified geodesic repair.

## Follow-up — persistent saliency selection, 2026-09-14
The user confirms geodesics now works, but repeated Show saliency clicks still
have no visible effect while the UI and camera remain responsive. This is not
evidence of a blocked frame loop. Keep the saliency defect open.

Diagnostic sources, commands, logs and the append-only probe ledger are retained
locally in `build/analysis/bug194-saliency-followup-2026-09-14/` (ignored). A direct
CPU keypoint run on the actual dragon produced 644 keypoints and 147,064 nonzero
scores in [0, 1.55736]. Show returned Applied; Vulkan readback observed scalar
mode, a nonzero scalar address and an automatically derived range. The captured
dragon had varying colors. This bounds the all-zero-output and missing-upload
hypotheses; it does not reproduce the user's failing desktop state.

A second diagnostic drove the real keypoint panel with AsyncWorkModule and
Vulkan frames. CPU detection published the field, the Show button selected it,
and GPU readback found it. Both the basic and phase-instrumented runs passed
using the registered general-GPU environment. Existing frame-phase diagnostics
measured substantial presentation waits both before and after Show; BUG-193
owns those observations. They do not match the user's responsive-camera symptom.

Source review confirms Show currently encodes a property to validate it before
applying the Appearance lane config; extraction encodes the displayed field
again. Show does not submit a new keypoint computation. Retained baked-texture
mode can invoke baking through the shared Appearance apply path, but was false
in these probes and is not an established cause here. Claude's bounded review
raised source hypotheses; none has yet reproduced this remaining defect.

The initial direct probe passed its assertions but exited on LeakSanitizer
retention after omitting the registered general-GPU environment (BUG-180).
An initial CTest probe exceeded the existing 30-second case budget; subsequent
large-model diagnostics used a separate finite timeout without changing the
registered gate. An initial Vulkan-method probe omitted SpatialIndexCache from
its test composition and could not submit detection. These failed harness runs
are not production regressions or passing verification.

After adding the SpatialIndexCache used by Sandbox main to the temporary
composition, Vulkan detection produced the field at 197.9 seconds of the UI
probe and activated Show. Final readback found scalar mode, a nonzero scalar
address and range [0, 1.23894e-05] on the camera-scaled dragon. The probe reached
its 200-second UI deadline before its extra confirmation frames, so the run
failed its timeout/ShowObserved assertions. This is evidence of field publication
and GPU selection, not a passing Vulkan-method test or a reproduced missing
display. All temporary source probes were removed after recording the results.

The remaining discriminators are the exact Display status, selected acceleration,
processing completion status and whether selecting the same property in Appearance
works. No additional production fix is justified by the successful CPU probes.

Cleanup verification: canonical ci configure/runtime-contract build passed;
15 visualization-recipe and 18 Appearance/editor contracts passed. The restored
ci-vulkan executable passed both registered saliency/Appearance recovery and
surface scalar/isoline readbacks. These maintain the existing regression checks;
they do not close the user-specific failure.

The user subsequently reports “A keypoint job for this output is already active.”
This is the Detect duplicate-job response, not the Display diagnostic. The guard
includes queued/running/dependency/gate/apply states. Saliency and mask publish
together after full detection, so an unfinished first run has no new output to
show. This narrows the investigation to job completion/progress and UI feedback;
it does not establish that the current job is progressing rather than stalled.
