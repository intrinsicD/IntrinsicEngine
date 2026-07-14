# BUG-018 — Sandbox hierarchy selection Vulkan ID target validation

## Goal
- Fix promoted-Vulkan sandbox hierarchy selection so selecting the ECS triangle from the scene hierarchy keeps the frame visible, records selection outline correctly, and does not emit `EntityId` / `PrimitiveId` transfer-source validation errors.

## Non-goals
- Reworking editor hierarchy UI or selection-controller semantics.
- Changing app-layer ownership, ECS component layout, or runtime selection authority.
- Adding new picking modes or changing primitive-refinement policy.
- Relaxing Vulkan validation, operational-gate, or fail-closed behavior.

## Context
- Symptom: launching `cmake-build-debug/bin/ExtrinsicSandbox` emitted Vulkan validation errors for `EntityId` / `PrimitiveId` layout transitions and `vkCmdCopyImageToBuffer(...)` because those R32_UINT images lacked transfer-source usage.
- Symptom: selecting `ReferenceTriangle` through the ImGui scene hierarchy enabled `SelectionOutlinePass`, but the promoted Vulkan path could either fail recipe validation or produce a mostly black selected frame.
- Expected behavior: `EntityId` / `PrimitiveId` are valid Vulkan copy sources only when host readback is active, hierarchy selection still produces a written `EntityId` target for outline sampling, and the outline shader samples `EntityId` rather than `SceneDepth`.
- Impact: the default sandbox's promoted Vulkan backend stayed fail-closed or presented a black frame even though ECS selection state was correct.
- Completed: 2026-06-07.
- PR/commit: pending local commit.

## Required changes
- [x] Add transfer-source usage to selection ID targets when picking readback can copy them.
- [x] Split `pickingActive` from the selection-ID pass gate so hierarchy/hover outline frames produce `EntityId` without importing `Picking.Readback`.
- [x] Keep no-depth-prepass selection-ID paths fail-closed instead of declaring outline reads with no valid ID producer.
- [x] Bind `EntityId` explicitly to frame-sampled descriptor slot 0 for `SelectionOutlinePass` instead of relying on the generic sorted-read fallback.
- [x] Extend mock RHI observations so CPU renderer lifecycle tests can assert sampled descriptor bindings.

## Tests
- [x] Add frame-recipe regression coverage for outline-only selection ID production without readback.
- [x] Add frame-recipe coverage for transfer-source usage on readback-enabled `EntityId` / `PrimitiveId` targets.
- [x] Add renderer lifecycle coverage proving `SelectionOutlinePass` binds `EntityId` into descriptor slot 0.
- [x] Add promoted-Vulkan runtime smoke coverage for hierarchy-selecting `ReferenceTriangle` and preserving a visible frame with outline.

## Docs
- [x] Add this closed bug task.
- [x] Update `tasks/backlog/bugs/index.md`.
- [x] Update `src/graphics/renderer/README.md` to document the selection-ID/readback split and explicit outline descriptor binding.

## Acceptance criteria
- [x] `EntityId` / `PrimitiveId` Vulkan validation errors for `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` are gone.
- [x] Hierarchy selection records `PickingPass`, `SelectionOutlinePass`, and `Present` on promoted Vulkan.
- [x] Hierarchy selection preserves the full-screen lit background instead of blacking out the window.
- [x] Focused CPU and opt-in `gpu;vulkan` regression tests pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -R 'FrameRecipeContract|RendererFrameLifecycle|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build cmake-build-debug --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicGraphicsVulkanSmokeTests ExtrinsicSandbox
/home/alex/Downloads/CLion-2025.2.1/clion-2025.2.1/bin/cmake/linux/x64/bin/ctest --test-dir cmake-build-debug --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke\.RecipeSelectorReachesOperationalVulkanCommandStream|RuntimeSandboxAcceptanceGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
timeout 10s stdbuf -oL -eL cmake-build-debug/bin/ExtrinsicSandbox
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Disabling validation layers or hiding Vulkan diagnostics.
- Making graphics code read live ECS/runtime state.
- Treating a GPU/Vulkan smoke as part of the default CPU gate.
- Reverting unrelated RUNTIME-097/UI-002/BUG-017 changes in the dirty worktree.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- This bug closes at `Operational` for the default sandbox promoted-Vulkan path because the opt-in `gpu;vulkan` hierarchy-selection smoke passed on this host.
