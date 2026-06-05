# BUG-014 — ExtrinsicSandbox ImGui black window regression

## Status

- Closed 2026-06-05 as **fixed and verified**.
- Maturity: `Operational` on Vulkan-capable hosts. The app-default promoted Vulkan path records `Present` and `ImGuiPass`, produces non-black backbuffer readback with validation enabled, and the default CPU/null gate remains green.
- Owner: local agent.
- Commit reference: this retirement commit on the current branch.

## Goal
- Restore the promoted `ExtrinsicSandbox` app path so the default app config reaches an operational Vulkan frame with visible non-black output and ImGui UI after the recent ImGui/font-atlas changes.

## Non-goals
- No broad renderer feature work beyond the black-window regression.
- No legacy graphics fallback or app-layer rendering shortcuts.
- No broad file-format, PBR, transparency, serialization, or performance claims.
- No removal of the validation-layer safety gate without a targeted justification.

## Context
- Symptom: running `./build/ci-vulkan/bin/ExtrinsicSandbox` shows a completely black window; previously ImGui windows were visible, but glyphs rendered as boxes.
- Expected behavior: the sandbox app should use the same promoted runtime/editor/default-recipe path as `RUNTIME-095`, with `SandboxEditorUi` visible and backed by a valid font atlas.
- Impact: existing `RUNTIME-095` and ImGui GPU smokes passed while the actual app regressed visually because they asserted command recording but not app-default ImGui/backbuffer pixels.
- Diagnosis: the runtime adapter produced real ImGui draw lists, and a temporary fragment-color probe proved the draw reached the render target. The black frame came from a Vulkan descriptor collision: framegraph sampled-texture bridge slots 1 and 2 were reserved for DebugView/Present, while real bindless texture leases could also allocate those slots. When the retained ImGui font atlas landed in slot 2, `Pass.Present` overwrote it with the black present-source texture and the ImGui shader sampled black alpha.
- Owner/layer: `graphics/vulkan` descriptor allocation and operational default-recipe execution. The app remains `app -> runtime` only.

## Required changes
- [x] Add a focused repro/diagnostic test that uses the app-default reference config rather than a test-only validation-disabled config.
- [x] Add a visible-output assertion through the existing backbuffer readback seam, so `Present` recording alone cannot hide a black frame.
- [x] Fix the smallest backend issue that prevents the app-default path from producing visible ImGui output: reserve Vulkan framegraph descriptor slots 0..2 and allocate real bindless textures from slot 3 upward.
- [x] Remove temporary debug instrumentation before closing the bug.

## Tests
- [x] Add or update `gpu;vulkan` regression coverage for the default sandbox app path.
- [x] Keep CPU/null ImGui adapter and renderer contracts passing.

## Docs
- [x] Update backend/renderer docs for the descriptor-slot reservation.
- [x] Update bug index/task state when the fix is verified.

## Acceptance criteria
- [x] Repro is documented and reliably covered by automated test(s).
- [x] `ExtrinsicSandbox` default app config reaches an operational Vulkan frame on a Vulkan-capable host.
- [x] The test seam distinguishes visible output from a black frame.
- [x] ImGui adapter and renderer contract tests still pass.
- [x] Fix does not introduce layering violations.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicGraphicsVulkanSmokeTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ExtrinsicSandboxDefaultConfigProducesVisibleFrameWithValidation' --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke|ImGuiSurfaceGpuSmoke|DefaultRecipeSurfaceGpuSmoke\.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples|DefaultRecipeSurfaceGpuSmoke\.RecipeSelectorReachesOperationalVulkanCommandStream' --timeout 120
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ImGui(Adapter|Pass|PresentContract)' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

Results: all listed checks passed on the fixing workspace. A bounded `timeout 6s ./build/ci-vulkan/bin/ExtrinsicSandbox`
run opened the app until the timeout killed it, which is expected for an interactive app binary.

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Reverting unrelated dirty worktree changes.
- Importing graphics/Vulkan/ImGui directly into `src/app/Sandbox`.
- Disabling validation globally to hide a real synchronization or layout bug.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; default CPU/null contracts must remain green.
