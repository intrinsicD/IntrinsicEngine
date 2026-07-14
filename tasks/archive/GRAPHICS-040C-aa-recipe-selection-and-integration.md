# GRAPHICS-040C — AA recipe selection + post-chain integration

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-05
- Completed: 2026-06-05
- Current slice: single-slice CPU/null `Operational` AA selector + reference-TAA
  recipe integration. The slice adds the explicit selector, a backend-neutral
  reconstruction pass/resource path, renderer diagnostics, and contract/runtime
  tests while leaving vendor backends unopened.
- Next verification step: retired. Vendor reconstructor backend children remain
  unopened until a concrete vendor SDK integration is selected.

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

## Slice plan

- **Slice A (this task).** Add the explicit AA selector and TAA/external
  reconstruction recipe path, including `MotionVectors`, retained history import,
  `ReconstructionPass`, post-reconstruction output sizing, and diagnostics. Prove
  the CPU/null route through frame-recipe contracts and renderer lifecycle tests.
  Defer vendor implementations to unopened per-vendor children.

## Required changes
- [x] Add the AA mode selector as an explicit recipe-build input (each mode compiles its
      expected pass set).
- [x] Route the resolve slot to the reconstructor for `TAA`/`ExternalReconstructor`;
      enable jitter + motion-vector production only for those modes.
- [x] Implement the input/output resolution split across the post-reconstruction chain.
- [x] Add the decision-9 diagnostics to `RenderDiagnostics`.
- [x] `contract;graphics` + integration tests for per-mode pass-set compilation and the
      resolution split.

## Tests
- [x] `contract;graphics` — each AA mode compiles its expected pass set; FXAA/SMAA
      unchanged; resolution split allocates the right extents.
- [x] `integration` — renderer lifecycle smoke records the TAA reconstruction pass and
      diagnostics; `NoJitterNoHistory` determinism is pinned by the existing camera
      contract plus the recipe-level temporal suppression test because no image-golden
      harness exists in this path.
- [x] CPU gate green.

## Docs
- [x] Document the AA selector + resolution policy in `src/graphics/renderer/README.md`
      and `docs/architecture/rendering-three-pass.md`.

## Acceptance criteria
- [x] AA modes are mutually exclusive and pin their pass sets; the reconstructor path is
      wired with the input/output resolution split.
- [x] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'FrameRecipeContract|GraphicsPostProcessChainContract|RendererFrameLifecycle' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
```

## Forbidden changes
- Changing the FXAA/SMAA passes.
- Importing a vendor SDK into promoted `graphics`.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Achieved: `Operational` — the AA selector + reference-TAA path run through the
  frame recipe and CPU/null renderer lifecycle. Vendor backends and opt-in
  `gpu;vulkan` reconstruction smokes remain unopened follow-ups.
