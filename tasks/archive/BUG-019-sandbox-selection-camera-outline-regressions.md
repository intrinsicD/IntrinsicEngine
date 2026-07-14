# BUG-019 — Sandbox selection, camera, and outline regressions

## Goal
- Restore the promoted sandbox editor path so camera controls, mouse picking, and hierarchy selection behave predictably: selecting the default triangle preserves scene color and applies only the selection outline.

## Non-goals
- Reintroducing legacy renderer/runtime ownership.
- Replacing the framegraph sampled-texture bridge with a full per-pass descriptor model in this slice.
- Implementing new mesh, graph, or point-cloud editing features beyond making the current UI wiring observable and safe.

## Context
- Owner/layers: `runtime` owns editor input and selection policy; `graphics/renderer` owns frame recipe and pass routing; `graphics/vulkan` owns the promoted backend descriptor bridge.
- `BUG-018` fixed selection-ID target validation but incorrectly made `SelectionOutlinePass` sample `EntityId` from frame-sampled descriptor slot 0, which is also the default postprocess/tonemap bridge slot.
- The Vulkan bridge records the frame into one command buffer against one bindless descriptor set, so the last host-side descriptor write to a slot is observed by every recorded draw at submit time.
- Completed: 2026-06-07.
- PR/commit: pending local commit.

## Required changes
- [x] Reserve a dedicated frame-sampled descriptor slot for `SelectionOutlinePass` and move promoted Vulkan real texture leases above the expanded reserved range.
- [x] Update `selection_outline.frag` and renderer pass routing so `EntityId` is sampled from the dedicated outline slot, not slot 0.
- [x] Add renderer contract coverage proving selection outline does not overwrite the default postprocess slot.
- [x] Tighten runtime/editor input behavior for camera and selection where a deterministic seam exists.

## Tests
- [x] Run focused graphics/runtime contract tests for selection outline and sandbox acceptance.
- [x] Build `ExtrinsicSandbox` so shader changes are compiled into the app output.
- [x] Run opt-in promoted Vulkan sandbox smoke coverage for visible default frame and hierarchy selection with outline.

## Docs
- [x] Update renderer/Vulkan bridge docs and bug index to reflect the corrected reserved-slot ownership.
- [x] Update runtime docs/UI text to expose current camera controls.

## Acceptance criteria
- [x] Scene hierarchy selection can enable `SelectionOutlinePass` without making the background red or the triangle black.
- [x] Mouse selection remains routed through runtime selection requests and is not hidden by descriptor-slot corruption.
- [x] Camera/input behavior is documented and covered by focused runtime seams in this slice.
- [x] The task is retired with exact verification commands recorded.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RuntimeSandboxAcceptance|RuntimeCameraControllers' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -R 'HierarchySelectionKeepsDefaultSandboxVisibleWithOutline|ExtrinsicSandboxDefaultConfigProducesVisibleFrameWithValidation' -L gpu -L vulkan --timeout 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making `graphics/*` depend on live ECS/runtime state.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- This bug closes at `Operational` for the promoted Vulkan sandbox path because the visible-frame and hierarchy-selection `gpu;vulkan` smokes passed on this host.
