---
id: BUG-163
theme: J
depends_on: [METHOD-039]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive user-directed diagnosis and repair; focused regressions, the reviewed diff, live isolated-app captures, and repository gates are the evidence."
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-03T11:42:43Z"
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "The repair changes the public curvature contract and exposes METHOD-039 as an explicit diagnostic runtime/config/UI choice with canonical mesh-property publication. It preserves METHOD-037 as the default, preserves source topology/cardinality, and does not introduce parameterization, support-radius, backend, or dependency-boundary contracts."
---
# BUG-163 — Sculpt curvature and feature-aligned patch rendering

## Goal
- Make `tests/data/sculpt.obj` use the corrected curvature behavior of the
  authoritative Framework24 comparison revision, produce a deterministic small
  feature-aligned curvature decomposition, and render the scalar and boundary
  results correctly in the Vulkan Sandbox.

## Non-goals
- No generic segmentation framework, global multicut library, GPU method
  backend, learned semantic labels, mesh cutting, or topology/cardinality edit.
- Do not reproduce Framework24's retired mixed-area defect, two-ring default,
  in-place smoothing race, or exact-extrema Jet presentation.
- Do not weaken METHOD-039's existing synthetic controls merely to accept one
  sculpt result.

## Context
- Symptom: current Intrinsic curvature anchors match an older Framework24
  checkout, while the authoritative clean comparison app at `6dd50a82` uses a
  corrected mixed Voronoi area, signed hinge, one-ring support, and no implicit
  smoothing. METHOD-039 is CPU-only and unadopted, so the Sandbox still runs
  METHOD-037.
- Expected behavior: the two apps agree numerically on identical sculpt
  coordinates; scalar publication reaches render residency; retained feature
  transitions are part boundaries; constant-curvature seed fragments merge to
  a small connected decomposition.
- Impact: the current combination can display a misleading smooth field and a
  segmentation unrelated to the METHOD-039 result the operator requested.

## Control surfaces
- Config/agent: the existing serializable curvature-segmentation config selects
  the explicitly diagnostic feature-aligned CPU candidate and applies it
  through the existing preview/validate/apply path; METHOD-037 remains default.
- UI: the Sandbox mesh-processing panel drives that same config and runtime
  operation, and exposes the published curvature/feature/boundary properties.

## Backends
- Backend axis: deterministic CPU reference method with backend-neutral
  publication; Vulkan is exercised only as the operational render consumer.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Owning oriented triangle-surface topology plus slot-aligned finite vertex principal-curvature scalars; faces and dual adjacency are semantic inputs. |
| Compatible entity sources | Mesh entities with materialized vertex/edge/face property domains and valid triangle topology. |
| RuntimeModule | Existing mesh geometry-processing operation validates the selected source, computes curvature and feature-aligned patches, and reports requested/actual method identity. |
| Config/agent | Existing versioned curvature-segmentation config gains the feature-aligned selector and parameters; preview/validate/apply remains shared by agents and UI. |
| UI | Existing Sandbox mesh curvature/segmentation panel exposes the selector, readiness, scalar choice, feature lines, final boundaries, and part colors. |
| Publication | Same-entity, same-cardinality vertex curvature scalars through the existing curvature transaction, plus edge feature/boundary masks and colors and face part IDs/colors through one segmentation transaction; unrelated properties and topology remain intact. |
| End-to-end tests | Sculpt numerical parity, METHOD-039 part/boundary contract, runtime publication/config, panel wiring, property-revision upload, and isolated Vulkan Sandbox execution. |

## Right-sizing
- Reuse the existing geometry functions, segmentation config, editor operation,
  canonical property publication, and visualization recipes. Add no service,
  registry, bridge, queue, interface, or second command path. A second backend
  or generic optimizer would justify a new seam; neither exists in this slice.

## Required changes
- [x] Replace the obsolete curvature oracle behavior with current Framework24
  default parity and retain fail-closed conditioning diagnostics.
- [x] Repair METHOD-039 merging/publication so promoted feature transitions are
  final boundaries and constant-curvature seed fragments collapse.
- [x] Bind the feature-aligned CPU candidate into shared config/runtime/UI as
  an explicit diagnostic selection, publish all curvature scalars through the
  existing curvature transaction, and publish feature/boundary/part values
  atomically through the segmentation transaction.
- [x] Ensure curvature compute activates the selected scalar visualization and
  segmentation activates face colors plus feature/final-boundary overlays.

## Tests
- [x] Freeze the current Framework24 sculpt field with readable anchors and a
  whole-field hash, plus analytic/scale/orientation regressions.
- [x] Add a sculpt decomposition regression requiring deterministic connected
  regions, 3–12 final parts, no unsupported seed-front boundary, and promoted
  feature edges included in the final boundary mask.
- [x] Cover config round-trip, runtime publication/undo/stale-source behavior,
  panel wiring, and property-driven render refresh.

## Docs
- [x] Correct curvature and METHOD-039 current-state method/architecture docs,
  supersede the stale local parity evidence, and update ARA claim dispositions.
- [x] Refresh the generated module inventory if a module interface changes.

## Acceptance criteria
- [x] Fresh probes of Framework24 `6dd50a82` and Intrinsic agree on all 3,669
  sculpt principal-curvature pairs within `2e-12` absolute error.
- [x] Every live sculpt face has one connected final region in a deterministic
  3–12-part result; every promoted interior feature transition is a final part
  boundary; every non-feature boundary has diagnosed curvature-closure support.
- [x] The Vulkan Sandbox, isolated under Xephyr, visibly renders the selected
  curvature scalar, feature lines, final boundaries, and part colors from the
  exact `tests/data/sculpt.obj` source without taking host input focus.
- [x] Focused method/runtime/UI tests, the default CPU gate, structural checks,
  and applicable Vulkan checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests IntrinsicCurvatureCorpusProbe
ctest --test-dir build/ci --output-on-failure -R 'CurvatureTensor|CurvaturePatch|CurvatureSegmentation|SandboxCurvatureSegmentationPanel|VisualizationRecipes' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Editing `experimental/framework24` or treating another checkout as the
  authoritative product-comparison source.
- Adding a UI-only tuning path or bypassing canonical property revisions.
- Claiming semantic object-part correctness or performance from this single
  sculpt regression.

## Maturity
- Target: `Operational` for the corrected curvature publication and for
  rendering the explicitly diagnostic METHOD-039 sculpt result through
  promoted Vulkan on a capable host. METHOD-039 does not become an accepted v2
  or production default; no GPU method backend is claimed or owed by this bug.
- Actual: `Operational` for that bounded CPU-publication/Vulkan-consumer scope.
  A promoted-Vulkan scalar-field/isoline readback executed rather than skipped,
  and isolated live Sandbox inspection exercised the exact sculpt asset. ARA
  C56 records this bounded result; ARA C55 remains a dirty-source numerical
  parity candidate pending BENCH-001's clean comparison custody.

## Log
- 2026-09-03: Operator explicitly requested Framework24/Sandbox comparison on
  sculpt, corrected rendering, feature-boundary inclusion, constant-curvature
  merging, and isolated display execution. The codebase resolves "METHOD-039"
  to its retired unadopted local grow/merge candidate; this bug promotes only a
  repaired version that passes the new sculpt and existing frozen controls.
- 2026-09-03: Ranked diagnosis before segmentation instrumentation: (1) stale
  Framework24 curvature semantics distort the descriptors; (2) retained soft
  features are advisory and can be internalized by merge/refinement; (3) the
  local seeded RAG stalls in many curvature-similar fragments; (4) runtime still
  invokes METHOD-037; (5) publication exists but the UI does not select the
  intended scalar/overlays. `BUG163-SCULPT-PATCH` records final/provisional
  region counts, feature retention, boundary roles, connectivity, and repeat
  determinism on the exact sculpt input; it will become a focused regression,
  with temporary diagnostic output removed after the hypotheses are separated.
- 2026-09-03: A fresh clean Framework24 `6dd50a8289c64b5054bc9601beb5647f459d7969`
  probe and the Intrinsic `ICURV002` probe consumed identical sculpt positions.
  Independent parsing found 3,669/3,669 supported and nonzero vertices, maximum
  absolute errors `5.440092820663267e-14` for minimum principal curvature and
  `8.237854842718662e-14` for maximum principal curvature, and quantized field
  hash `0xfc090818c136a6e2`. The durable anchors and bounded disposition are in
  `ara/evidence/tables/curvature_framework24_current_parity_2026-09-03.md`.
- 2026-09-03: The fixed-six, complexity-`0.5` sculpt profile produced eight
  connected parts with face counts `[160,160,168,628,632,758,1488,3348]`.
  Its 620 final boundaries comprise all 384 hard features plus 236
  soft-supported transitions and zero unsupported closure edges. Moving every
  seed one legal dual step preserves the eight-part count, boundary mask, and
  boundary roles. Exact phase-end energy backchecks retain the original
  objective while avoiding a full-mesh recomputation after every accepted
  local move.
- 2026-09-03: Renderer diagnosis found that OBJ corner normals/UVs expand the
  3,669 canonical sculpt vertices to 17,795 GPU surface vertices while the
  scalar buffer remained canonical. Runtime now retains the mesh packer's exact
  source-vertex map, expands only CPU-backed mesh-surface vertex properties,
  and stamps that layout independently so an equal-sized topology reorder
  cannot reuse stale residency. The regression freezes uploads
  `[2,3,1,3,4,1]` and, after a face-slot reorder, `[3,4,1,2,3,1]`.
- 2026-09-03: Framework24 and Sandbox were run on isolated Xephyr display `:97`
  with `-no-host-grab`. Live Sandbox inspection imported 3,669 vertices/7,342
  faces, rendered the selected principal scalar through the 17,795-entry GPU
  stream, and rendered the eight part colors plus 620 final boundary edges.
  The diagnostic panel reported requested=actual METHOD-039, 384 hard/808
  retained-soft candidates, boundary roles 384/236/0, and energy
  `873.911 -> 8.61381`. Each owned Xephyr process was stopped after use.
- 2026-09-03: Verification passed: 113 focused method/runtime/UI/render tests;
  the final default CPU selector 4,263/4,263; isolated ASan and UBSan selectors
  2,748/2,748 each; and the promoted-Vulkan test
  `RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleScalarFieldSurfaceAndIsolinesResolveOnGpu`
  1/1 on the final source state. The CPU/sanitizer selectors each reported the
  expected GLFW/LSan capability skip. Strict layering, task policy, doc links,
  ARA claims, method manifests, test layout, root hygiene, diff checks, and the
  clean-workshop validator passed; regenerating the module inventory produced
  no diff.
- 2026-09-03: Pre-merge review found one scoped intent and no drive-by cleanup;
  changed behavior has focused, full-CPU, sanitizer, and real-Vulkan coverage;
  public surfaces use owning/lower-layer types; Runtime remains the composition
  owner; config/file/UI share one validated apply path; buffer payload and
  sidecar lifetimes remain owning and render-thread-local; invalid remaps fail
  closed; no shader, descriptor-set, push-constant, pass-ID, or recipe-edge
  contract changed. Clean-workshop rows: 1 pass, 2 pass, 3 pass, 4 pass,
  5 n/a, 6 n/a, 7 pass, 8 n/a; no follow-up finding was opened.
