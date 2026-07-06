---
id: BUG-056
theme: G
depends_on: []
maturity_target: Operational
completed: 2026-07-06
---
# BUG-056 — ExtrinsicSandbox default Vulkan validation gate fallback

## Status
- Retired on 2026-07-06 at `Operational` on local `main`; PR not opened.
- PR/commit: this retirement commit.
- The default deferred GBuffer fragment now consumes the full shared
  `default_debug_surface.vert` interface, the frame-pacing capture report
  records final `IDevice::IsOperational()`, and the validator fails
  shader-interface warnings or a final non-operational
  `BarrierValidationFailed` path.

## Goal
- Make the default `ExtrinsicSandbox` `ci-vulkan` frame-pacing capture reach an
  operational Vulkan frame, or fail/skip with deterministic diagnostics, by
  fixing the shader or pipeline-interface validation warnings that currently
  trip the promoted Vulkan operational gate.

## Non-goals
- Do not loosen the promoted Vulkan validation gate.
- Do not hide the issue by disabling validation layers, VSync, barriers, or the
  default present-mode policy.
- Do not change the frame-pacing diagnostic schema from `UI-030` unless the
  fix needs an explicit operational-status field.

## Context
- Symptom: the `UI-030` default capture on 2026-07-05 logged
  `VulkanRequestedButNotOperational status=RequestedButIncompleteGate
  reason=BarrierValidationFailed`.
- Symptom: validation output reported vertex shader outputs at locations `4`,
  `8`, and `3` that were not consumed by the fragment shader.
- Expected behavior: default `ExtrinsicSandbox` under `ci-vulkan` should either
  become operational for the bounded frame-pacing capture, or deterministically
  skip/fail because the host environment lacks Vulkan support.
- Impact: `UI-030` can rank the current default capture as present/fallback
  dominated, but it cannot use that capture as proof of operational Vulkan
  acquire/present/fence behavior until the validation gate is clean.

## Required changes
- [x] Reproduce the gate fallback with
      `ExtrinsicSandbox --frame-pacing-report <path> --frame-pacing-frames 16`
      under `ci-vulkan`.
- [x] Identify the pipeline/shader pair that emits the SPIR-V interface
      mismatch and either remove unused vertex outputs or add matching fragment
      inputs where the data is genuinely consumed.
- [x] Ensure the promoted Vulkan operational-readiness gate can promote the
      default sandbox recipe after validation-clean frame setup.

## Tests
- [x] Add or extend opt-in `gpu;vulkan` regression coverage around
      `ExtrinsicSandbox.FramePacingDiagnosticCapture` so the default capture no
      longer accepts `VulkanRequestedButNotOperational
      reason=BarrierValidationFailed` as a passing operational path.
- [x] Run the selected promoted Vulkan sandbox/ImGui/frame-lifecycle smoke
      tests after the fix.

## Docs
- [x] Update the UI-030 report or the relevant rendering/Vulkan docs if the fix
      changes gate semantics or the diagnostic output contract.

## Acceptance criteria
- [x] The default frame-pacing capture reports no shader-interface validation
      warnings for the default sandbox recipe.
- [x] The default capture reaches operational Vulkan on a Vulkan-capable host,
      or deterministically skips/fails only for environment capability reasons.
- [x] The fix preserves graphics/runtime/platform layering.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicGraphicsVulkanContractTests
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ExtrinsicSandbox.FramePacingDiagnosticCapture' -L 'gpu' -L 'vulkan' --timeout 180
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke\.(ImportedOffOriginObjTriangleAutoFramesAtCenter|ImportedObjWithoutAuthoredUvsSamplesGeneratedAlbedoTexture)' -L 'gpu' -L 'vulkan' --timeout 180
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ExtrinsicSandbox.FramePacingDiagnosticCapture|RuntimeSandboxAcceptanceGpuSmoke|FrameLifecycle|ImGuiSurface' -L 'gpu' -L 'vulkan' --timeout 180
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RuntimeVulkanBreadcrumb|FramePacingDiagnostics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
LSAN_OPTIONS='suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp:fast_unwind_on_malloc=0:detect_leaks=0' ASAN_OPTIONS='symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0' timeout 60s build/ci-vulkan/bin/ExtrinsicSandbox --frame-pacing-report /tmp/bug056-frame-pacing-fixed.json --frame-pacing-frames 16
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Downgrading validation, VSync, framegraph barriers, or present-mode policy to
  make the gate pass.
- Introducing runtime imports into graphics or graphics imports into platform.

## Maturity
- Retired at `Operational` on a Vulkan-capable local host. The direct capture
  produced `final_device_operational: true` and no `SPIR-V Interface` / `not an
  Input declared` warning matches.
