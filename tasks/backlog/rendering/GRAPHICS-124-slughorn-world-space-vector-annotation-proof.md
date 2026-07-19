---
id: GRAPHICS-124
theme: B
depends_on:
  - REVIEW-003
maturity_target: CPUContracted
---
# GRAPHICS-124 — Slughorn world-space vector annotation proof

## Goal

- Determine, in an isolated CPU/Vulkan evidence harness, whether one pinned
  Slughorn revision evaluates a bounded pre-shaped curve subset accurately and
  robustly enough to justify a later consumer-backed engine integration task.

## Non-goals

- No ImGui replacement, editor UI, document system, font manager, retained
  annotation scene, text-layout/shaping framework, fallback-font policy, or
  localization work.
- No general SVG importer or arbitrary file IO; fixtures are checked-in,
  bounded, and pre-shaped.
- No production annotation packet, `RuntimeRenderSnapshotBatch` lane,
  `Graphics.VisualizationPackets` extension, frame-recipe pass/resource,
  renderer branch, runtime producer, shader asset, or config/UI surface.
- No change to the existing transient-debug or visualization-overlay paths;
  retired `RUNTIME-177` deliberately introduced no generic runtime producer
  seam without a named production caller.
- No new renderer interface, pass registry, service, bridge, queue, event
  chain, or public module.
- No claim that the external project is production-ready beyond the tested
  pinned revision and bounded harness.

## Context

- Owner/layer: a standalone evidence executable and its checked fixtures live
  under rendering benchmark/test support. It may consume existing public RHI
  and Vulkan test seams, but adds no production `src/graphics` or `src/runtime`
  surface.
- Retired `GRAPHICS-010` and `GRAPHICS-014` already provide transient debug
  and visualization packet/pass patterns. They are comparison context only:
  this task must not widen either path without a named method/UI consumer and a
  separate integration task.
- Sources: [Slughorn repository](https://github.com/AlphaPixel/SlugHorn),
  [Slughorn project article](https://alphapixeldev.com/slughorn-mit-licensed-gpu-agnostic-slug-font-glyph-rendering-library-for-opengl-osg-vulkan-and-all-gpu-driven-graphics-apis/),
  the [Slug vector-graphics paper](https://jcgt.org/published/0006/02/02/), and
  [Graphics Programming Weekly Issue 445](https://www.jendrikillner.com/post/graphics-programming-weekly-issue-445/).
- Third-party policy: if Slughorn is used, pin one audited revision through a
  repository vcpkg overlay/manifest entry. No FetchContent or ad-hoc vendoring.

## Right-sizing

- Element under evaluation: one pinned library revision and one bounded
  task-local curve evaluator, not an engine annotation feature.
- Simpler alternative: task-local plain records feed a standalone offscreen
  harness directly; no packet/snapshot/recipe vocabulary is introduced.
- Blast radius: vcpkg metadata, one evidence executable, test-local shader and
  fixtures, CPU oracle, and an evidence report. Production `src/`, runtime,
  recipes, and shipped shader assets remain untouched.
- Reintroduction trigger: an integration task may open only when this evidence
  passes and a named UI/method consumer needs the capability. That task must
  decide whether to extend `Graphics.VisualizationPackets` and an existing
  recipe stage; this evidence task does not pre-decide either surface.

## Required changes

- [ ] Pin the audited Slughorn revision through vcpkg manifest/overlay metadata,
      limiting enabled dependencies/features to those required by the harness
      and linking it only to the evidence target.
- [ ] Define task-local pointer-free curve/control records with bounded
      transforms, colors, depth convention, and stable sample IDs. Reject
      invalid, non-finite, or over-budget records deterministically; do not
      export them as an engine packet.
- [ ] Implement an independent CPU coverage evaluator for the bounded curve
      subset and checked fixtures: a glyph outline, an open curve, an SVG-like
      icon, and a world-space axis/label composition.
- [ ] Implement one isolated offscreen Vulkan harness using existing public
      test/device seams and test-local shader inputs. It must not register a
      renderer pass, modify a frame recipe, or add runtime extraction/wiring.
- [ ] Compare CPU and GPU sampled coverage/color under orthographic,
      perspective, 16x magnification, oblique view, partial viewport clipping,
      depth-tested, and overlay-depth-disabled cases.
- [ ] Expose only scalar diagnostics needed by the proof: submitted/rejected
      annotation count, curve/control-point count, clipped count, and budget
      overflow count.
- [ ] Record an evidence verdict. A negative or dependency-unusable result
      closes the task without an integration follow-up; a positive result only
      permits a separate task after it names a real consumer.

## Tests

- [ ] CPU unit/contract tests cover record validation, deterministic bounds,
      fixture coverage samples, empty input, non-finite transforms, clipping,
      and budget rejection.
- [ ] An opt-in `gpu;vulkan` harness test compares selected CPU/GPU coverage
      samples and proves non-background offscreen output in ordinary and
      oblique views without entering the production renderer.
- [ ] Existing ImGui and transient-debug GPU smokes remain green.

## Docs

- [ ] Document the task-local record schema, bounded curve subset, offscreen
      target/depth convention, diagnostics, pinned dependency revision, and
      CPU/GPU parity tolerance in an evidence report.
- [ ] State explicitly that ImGui, shaping/fallback, persistent annotations,
      arbitrary SVG ingestion, production packets, and recipe/runtime
      integration remain out of scope.

## Acceptance criteria

- [ ] Checked fixtures execute through task-local CPU and offscreen Vulkan
      paths without adding or changing a production module, packet, pass,
      recipe, shader, runtime seam, or main-loop branch.
- [ ] CPU and Vulkan coverage samples agree within documented tolerance on all
      required camera/scale/depth fixtures.
- [ ] An actually run `gpu;vulkan` harness test proves visible world-space
      vector output and records bounded diagnostics.
- [ ] The evidence report records a positive or negative verdict and forbids
      integration work without both passing evidence and a named consumer.
- [ ] ImGui and existing transient-debug behavior remain unchanged.
- [ ] Default CPU and structural gates pass.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Slughorn|VectorCoverage|TransientDebug' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'Slughorn|VectorCoverage' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- FetchContent, source copying, or dependency acquisition outside the vcpkg
  manifest/overlay path.
- ImGui replacement, editor controls, font/shaping framework, retained scene,
  arbitrary SVG importer, or unrelated visualization features.
- Any production `src/graphics`/`src/runtime` change, annotation packet,
  snapshot-batch lane, recipe/pass/resource contribution, shipped shader, or
  config/UI integration.
- Live ECS/runtime/editor knowledge in the harness or backend types in public
  APIs.
- New service/registry/manager/factory/queue abstractions.
- Opening an engine-integration task without a named consumer and a positive
  recorded evidence verdict.

## Maturity

- Target: `CPUContracted`; the intended endpoint is a reproducible evidence
  verdict. An opt-in Vulkan harness result does not make an engine annotation
  capability `Operational`, and no `Operational` follow-up is owed.
