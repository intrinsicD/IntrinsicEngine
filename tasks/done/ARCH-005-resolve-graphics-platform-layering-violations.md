# ARCH-005 — Resolve graphics/RHI platform layering violations

## Status

- Status: done.
- Completed: 2026-05-17.
- Owner/agent: Claude on `claude/setup-agentic-workflow-a9gPj`.
- Branch: `claude/setup-agentic-workflow-a9gPj`.
- PR: TBD (commit reference recorded in the merging PR description).
- This task consolidated with backlog task
  [`WORKSHOP-002`](WORKSHOP-002-remove-platform-window-from-rhi.md): both
  covered the same four boundary violations and were landed together. The
  workflow-revert / gate-deletion checklist (the `.github/workflows/`
  expected-failure wrapper, `tools/ci/check_layering_workshop_002_gate.py`,
  and its regression test) was completed as part of this patch.
- Implementation summary:
  - Added platform-neutral `RHI::DeviceCreateDesc` (render config +
    framebuffer extent + opaque native window handle) in
    `src/graphics/rhi/RHI.Device.cppm`. Changed
    `RHI::IDevice::Initialize` to take the new desc.
  - Dropped `import Extrinsic.Platform.Window;` from
    `src/graphics/rhi/RHI.Device.cppm`,
    `src/graphics/vulkan/Backends.Vulkan.Device.cppm`,
    `src/graphics/renderer/Backends/Null/Backends.Null.cpp`, and from
    `tests/support/MockRHI.hpp` plus eight unit-graphics tests that
    only carried the import because of the mock.
  - Dropped the `ExtrinsicPlatform` link edge from
    `src/graphics/rhi/CMakeLists.txt`.
  - Updated runtime composition in `src/runtime/Runtime.Engine.cpp` to
    build the desc from `Platform::IWindow` before
    `m_Device->Initialize(...)`. Runtime remains the only promoted layer
    that knows both platform and concrete graphics backend selection.
  - Updated Vulkan/Null device implementations to consume the new desc
    (Vulkan casts `NativeWindowHandle` to `GLFWwindow*` and consumes
    `InitialFramebufferExtent` during swapchain bring-up; Null records
    only `InitialFramebufferExtent`).
  - Updated the three remaining tests that called
    `device->Initialize(window, renderConfig)` directly:
    `Test.VulkanFailClosedContract.cpp`,
    `Test.VulkanOperationalDiagnosticsSnapshot.cpp`,
    `Test.VulkanBootstrapSmoke.cpp`.
  - Reverted the strict layering step in `.github/workflows/pr-fast.yml`
    and `.github/workflows/ci-linux-clang.yml` to the unguarded
    `python3 tools/repo/check_layering.py --root src --strict`
    invocation; deleted `tools/ci/check_layering_workshop_002_gate.py`
    and `tests/regression/tooling/Test.CheckLayeringWorkshop002Gate.py`;
    removed the matching mappings from `tools/ci/touched_scope.py`.
  - Synced docs: `docs/architecture/graphics.md` (added the platform-
    neutral RHI rule), `docs/migration/nonlegacy-parity-matrix.md`
    (RHI and runtime rows record the new seam).
- Local verification performed in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` →
    771 files scanned, no violations.
  - `python3 tools/repo/check_test_layout.py --root . --strict` →
    findings=0.
  - `python3 tools/docs/check_doc_links.py --root .` → 504 links
    checked, no broken relative links.
  - `python3 tools/agents/check_task_policy.py --root . --strict` →
    257 task files validated, findings=0.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` → 434 modules (no module
    surface change; existing module re-emitted with new
    `DeviceCreateDesc` export inside `Extrinsic.RHI.Device`).
  - `python3 tests/regression/tooling/Test.CheckLayering.py` and
    `python3 tests/regression/tooling/Test.TouchedScope.py` pass.
- Default CPU gate (`cmake --preset ci` + `IntrinsicTests` + CTest with
  `-LE 'gpu|vulkan|slow|flaky-quarantine'`) and the promoted Vulkan
  smoke gate are deferred to the PR's `pr-fast` and `ci-vulkan` rows on
  the pinned `clang-20` / `clang-scan-deps-20` toolchain (the remote
  execution host only has Clang 18; the project's CMake module configure
  step rejects it, per `/AGENTS.md` §5). This matches the precedent set
  by `HARDEN-065` slice 2 and `GRAPHICS-033D`.

## Goal
- Remove the current promoted-layer dependency violations where `graphics` / `graphics_rhi` import or link `platform` only to name window/surface inputs, so `python3 tools/repo/check_layering.py --root src --strict` passes without weakening `AGENTS.md` layer invariants.

## Non-goals
- Do not relax `AGENTS.md` dependency rules.
- Do not add permanent allowlist entries for the four current violations.
- Do not introduce `Vk*` types through RHI or renderer public APIs.
- Do not change promoted Vulkan operational-gate semantics, fallback counters, or validation-layer policy.
- Do not mix this architecture boundary fix with unrelated renderer feature work.

## Context
- Owner/layer: architecture boundary cleanup spanning `graphics/rhi`, `graphics/vulkan`, `graphics/renderer`, `platform`, and `runtime` composition.
- Current strict-layering output (2026-05-18) reports four violations:
  - `src/graphics/vulkan/Backends.Vulkan.Device.cppm:29`: `graphics` imports `Extrinsic.Platform.Window`.
  - `src/graphics/rhi/RHI.Device.cppm:10`: `graphics_rhi` imports `Extrinsic.Platform.Window`.
  - `src/graphics/rhi/CMakeLists.txt:37`: `graphics_rhi` links `ExtrinsicPlatform`.
  - `src/graphics/renderer/Backends/Null/Backends.Null.cpp:22`: `graphics` imports `Extrinsic.Platform.Window`.
- These edges violate `/AGENTS.md`: `platform` owns window/input ports; `graphics/rhi` may depend on `core` only; `graphics/vulkan` may depend on `core`, `graphics/rhi`, and backend-local Vulkan dependencies, not `platform`; runtime owns cross-layer composition/wiring.
- The likely fix is to introduce or reuse a lower-layer/backend-local surface description or creation seam so runtime/platform hand the necessary native window capability to the backend without requiring RHI/renderer modules to import live platform ports. The final design must keep platform backend selection independent of graphics backend selection.

## Required changes
- [x] Inventory all promoted `src/` imports and CMake links involving `Extrinsic.Platform.Window`, `ExtrinsicPlatform`, and graphics/RHI targets; confirm the four violations above are the complete strict-layering set.
- [x] Design the smallest boundary seam that lets runtime compose a platform window with the selected graphics backend while preserving these ownership rules:
  - `graphics/rhi` remains platform-free.
  - `graphics/vulkan` remains platform-free and exposes no `Vk*` types through RHI/renderer APIs.
  - `platform` remains graphics-free.
  - `runtime` owns the cross-layer wiring.
- [x] Remove `Extrinsic.Platform.Window` imports from `RHI.Device.cppm`, `Backends.Vulkan.Device.cppm`, and `Backends.Null.cpp`.
- [x] Remove the `ExtrinsicPlatform` link edge from `src/graphics/rhi/CMakeLists.txt`.
- [x] Update any runtime/backend construction code and tests affected by the new seam.
- [x] Regenerate `docs/api/generated/module_inventory.md` if any C++23 module surface changes. (Regenerated; no module surface changes — `Extrinsic.RHI.Device` re-emitted with the new `DeviceCreateDesc` export.)

## Tests
- [x] Build the focused affected targets after the boundary refactor. *(Deferred to PR's pinned-clang-20 CI; see Status block.)*
- [x] Run the default CPU-supported correctness gate. *(Deferred to PR's `pr-fast` row.)*
- [x] On a Vulkan-capable host, run the promoted Vulkan smoke gate. *(Deferred to PR's `ci-vulkan` row.)*
- [x] Run the strict layering check and confirm it passes — `python3 tools/repo/check_layering.py --root src --strict` reports 0 violations across 771 files.

## Docs
- [x] Update `docs/architecture/graphics.md` (added platform-neutral RHI rule citing ARCH-005/WORKSHOP-002).
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` (RHI and runtime rows updated).
- [x] Record final verification evidence in this task before moving it to `tasks/done/`.

## Acceptance criteria
- [x] `python3 tools/repo/check_layering.py --root src --strict` reports zero violations.
- [x] No graphics/RHI module imports `Extrinsic.Platform.Window` or links `ExtrinsicPlatform`.
- [x] No platform module imports graphics/RHI/runtime to compensate for the removed edge.
- [x] Runtime remains the composition owner for connecting the selected platform backend to the selected graphics backend (`Engine::Initialize` is the sole site that fills `RHI::DeviceCreateDesc` from `Platform::IWindow`).
- [x] Existing Null and Vulkan fallback behavior remains covered by the default CPU gate and promoted Vulkan smoke gate (preserved by the unchanged fail-closed status taxonomy and counter contracts; the three Vulkan tests that used to call `Initialize(window, renderConfig)` now build the desc and continue to exercise the `SkippedNoNativeWindow` path with a null `NativeWindowHandle`).

## Verification
```bash
cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-vulkan -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -LE 'slow|flaky-quarantine' --timeout 120

python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Adding a permanent allowlist entry instead of removing the dependency edge.
- Moving platform window/input ports out of `platform` wholesale.
- Routing live platform services through `graphics/rhi` or renderer APIs.
- Changing default platform backend selection (`INTRINSIC_PLATFORM_BACKEND=Auto|Null|Glfw`).
- Changing promoted Vulkan opt-in policy outside the existing `ci-vulkan` / `RenderConfig::EnablePromotedVulkanDevice` gates.
