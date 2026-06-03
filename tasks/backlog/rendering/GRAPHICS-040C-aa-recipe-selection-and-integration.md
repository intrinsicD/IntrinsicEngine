# GRAPHICS-040C — AA recipe selection + post-chain integration

## Goal
- Add the mutually-exclusive AA mode selector `{ NoAA, FXAA, SMAA, TAA,
  ExternalReconstructor }` to the frame recipe and wire the TAA/reconstructor path
  (jitter + motion vectors enabled, resolve slot routed to the reconstructor, input/output
  resolution policy) (`GRAPHICS-040` decisions 6/7/9), with integration tests.

## Non-goals
- No vendor backends (deferred per-vendor children, not opened).
- No change to the unchanged FXAA/SMAA passes (decision 6).

## Context
- Owner layer: `graphics/renderer` (recipe selection + post-chain routing).
- Depends on `GRAPHICS-040B` (`IReconstructor` + reference TAA) and `GRAPHICS-013A`/
  `GRAPHICS-075` (postprocess chain, done).
- Decision 6: the recipe selects exactly one AA mode; FXAA/SMAA passes are unchanged;
  selecting `TAA`/`ExternalReconstructor` enables jitter + motion-vector production and
  routes the post chain's resolve slot to the reconstructor; the selector is an explicit
  recipe-build input.
- Decision 7: the reconstructor declares `InputExtent` vs `OutputExtent`; the graph
  allocates `SceneColorHDR`/depth/motion at input resolution and the post-reconstruction
  chain (tone map, debug view, UI, present) at output resolution; `InputExtent ==
  OutputExtent` runs as pure AA. Decision 9: `ReconstructorAppliedFrames`,
  `HistoryDisocclusionPercent`, `JitterOffsetX/Y` on `RenderDiagnostics`.

## Required changes
- [ ] Add the AA mode selector as an explicit recipe-build input (each mode compiles its
      expected pass set).
- [ ] Route the resolve slot to the reconstructor for `TAA`/`ExternalReconstructor`;
      enable jitter + motion-vector production only for those modes.
- [ ] Implement the input/output resolution split across the post-reconstruction chain.
- [ ] Add the decision-9 diagnostics to `RenderDiagnostics`.
- [ ] `contract;graphics` + integration tests for per-mode pass-set compilation and the
      resolution split.

## Tests
- [ ] `contract;graphics` — each AA mode compiles its expected pass set; FXAA/SMAA
      unchanged; resolution split allocates the right extents.
- [ ] `integration` — `NoJitterNoHistory` golden-image determinism for the TAA path.
- [ ] CPU gate green.

## Docs
- [ ] Document the AA selector + resolution policy in `src/graphics/renderer/README.md`
      and `docs/architecture/rendering-three-pass.md`.

## Acceptance criteria
- [ ] AA modes are mutually exclusive and pin their pass sets; the reconstructor path is
      wired with the input/output resolution split.
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing the FXAA/SMAA passes.
- Importing a vendor SDK into promoted `graphics`.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` — the AA selector + reference-TAA path run in the recipe and are
  golden-image/integration-tested on the CPU/null gate; an opt-in `gpu;vulkan`
  reference-TAA resolve smoke may follow but is not owed for the CPU contract.
