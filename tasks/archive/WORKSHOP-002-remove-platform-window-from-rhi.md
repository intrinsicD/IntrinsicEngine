# WORKSHOP-002 — Remove platform/window ownership from RHI

## Status

- Status: done.
- Completed: 2026-05-17.
- Owner/agent: Claude on `claude/setup-agentic-workflow-a9gPj`.
- Branch: `claude/setup-agentic-workflow-a9gPj`.
- PR: TBD (commit reference recorded in the merging PR description).
- Landed jointly with `ARCH-005` (same boundary fix, same patch). See
  [`ARCH-005`](ARCH-005-resolve-graphics-platform-layering-violations.md)
  for the full implementation summary and local verification evidence.

## Goal
- Restore the intended `graphics/rhi -> core` boundary by removing direct platform dependency from `ExtrinsicRHI` and moving window/surface-specific initialization responsibility into runtime/backend factory wiring.

## Non-goals
- Do not redesign the entire Vulkan backend.
- Do not implement new rendering features.
- Do not change the public renderer frame lifecycle beyond what is required to remove `Platform::IWindow` from the abstract RHI surface.
- Do not delete legacy Vulkan/RHI paths.

## Context
- `RHI::IDevice` currently imports `Extrinsic.Platform.Window` and takes `Platform::IWindow&` in `Initialize(...)`.
- `src/graphics/rhi/CMakeLists.txt` currently links `ExtrinsicPlatform`.
- This violates the intended promoted contract where `graphics/rhi` depends on `core` only.
- Runtime should own composition between platform window and concrete backend. Abstract RHI should stay backend-neutral and platform-neutral.
- WORKSHOP-001 (retired 2026-05-17) made this violation detectable in
  `check_layering.py --strict`; this task fixes it. WORKSHOP-001 also
  surfaced three sibling graphics-layer imports of
  `Extrinsic.Platform.Window` —
  `src/graphics/renderer/Backends/Null/Backends.Null.cpp`,
  `src/graphics/vulkan/Backends.Vulkan.Device.cppm` — which this task
  must also remove, because they exist only to satisfy the current
  `IDevice::Initialize(Platform::IWindow&, ...)` contract.
- Until this task lands, `.github/workflows/pr-fast.yml` and
  `.github/workflows/ci-linux-clang.yml` wrap the strict layering check
  as an expected-failure invocation that succeeds while
  `Extrinsic.Platform.Window` is reported. Reverting those steps to the
  unguarded `python3 tools/repo/check_layering.py --root src --strict`
  invocation is part of this task's Required changes.

## Required changes
- [x] Replace `RHI::IDevice::Initialize(Platform::IWindow&, const Core::Config::RenderConfig&)` with a platform-neutral initialization API. *(Now `Initialize(const RHI::DeviceCreateDesc& desc)`.)*
- [x] Introduce a minimal platform-neutral RHI device creation descriptor — `RHI::DeviceCreateDesc { RenderConfig; InitialFramebufferExtent; NativeWindowHandle; }` declared in `Extrinsic.RHI.Device`.
- [x] Move window/surface handoff into runtime composition: `Engine::Initialize` (in `src/runtime/Runtime.Engine.cpp`) fills the desc from the live `Platform::IWindow` and hands it to `RHI::IDevice::Initialize`. The Vulkan backend casts `NativeWindowHandle` to `GLFWwindow*` internally; the factory function `Backends::Vulkan::CreateVulkanDevice()` keeps its zero-arg signature so `graphics/vulkan` stays platform-import-free.
- [x] Ensure `ExtrinsicRHI` no longer imports any `Extrinsic.Platform.*` module.
- [x] Ensure `src/graphics/rhi/CMakeLists.txt` no longer links `ExtrinsicPlatform`.
- [x] Ensure `src/graphics/renderer/Backends/Null/Backends.Null.cpp` and `src/graphics/vulkan/Backends.Vulkan.Device.cppm` no longer import `Extrinsic.Platform.Window`.
- [x] Keep Null/headless behavior intact (Null device records `desc.InitialFramebufferExtent` and remains the default fallback).
- [x] Update all concrete device implementations and mocks to match the new initialization contract (`tests/support/MockRHI.hpp` now uses the desc; unit graphics tests dropped the now-unused `Extrinsic.Platform.Window` import).
- [x] Update runtime device selection/wiring so platform-aware code lives in `runtime` and concrete backend factory code, not in abstract RHI.
- [x] No temporary bridge needed.
- [x] Revert the `Validate layering` step in `.github/workflows/pr-fast.yml` and `.github/workflows/ci-linux-clang.yml` to the unguarded `python3 tools/repo/check_layering.py --root src --strict` invocation; delete `tools/ci/check_layering_workshop_002_gate.py` and `tests/regression/tooling/Test.CheckLayeringWorkshop002Gate.py`; remove the matching mappings from `tools/ci/touched_scope.py`.

## Tests
- [x] Existing RHI manager tests now compile without `ExtrinsicPlatform` as an RHI dependency (and dropped the now-unused `Extrinsic.Platform.Window` import).
- [x] Contract test proving `src/graphics/rhi/**` contains no `Extrinsic.Platform` imports: enforced by `python3 tools/repo/check_layering.py --root src --strict` (now unguarded in CI) plus the negative fixture `tests/contract/repo/layering_fixtures/negative_rhi_imports_platform_window/` exercised by `tests/regression/tooling/Test.CheckLayering.py`.
- [x] Contract test proving `ExtrinsicRHI` target does not link `ExtrinsicPlatform`: enforced by the same strict layering check (the CMake link-edge scan) plus `negative_rhi_links_platform`.
- [x] Runtime device-selection tests: `Test.VulkanFailClosedContract`, `Test.VulkanOperationalDiagnosticsSnapshot`, and `Test.VulkanBootstrapSmoke` updated to build the new desc; they continue to assert `SkippedNoNativeWindow` on the null-handle path.
- [x] Vulkan fail-closed contract tests continue to compile against the Vulkan backend.

## Docs
- [x] Updated `docs/architecture/graphics.md` to state that `graphics/rhi` is platform-neutral and to document the `RHI::DeviceCreateDesc` seam.
- [x] Runtime composition ownership reaffirmed in `docs/migration/nonlegacy-parity-matrix.md` (runtime row); `docs/architecture/runtime-subsystem-boundaries.md` was already accurate (it states runtime owns the cross-layer wiring).
- [x] Updated `docs/migration/nonlegacy-parity-matrix.md` RHI and runtime rows to reflect the corrected ownership.
- [x] Regenerated module inventory (no module surfaces added/removed; `Extrinsic.RHI.Device` re-emitted with the new `DeviceCreateDesc` export).

## Acceptance criteria
- [x] `src/graphics/rhi/RHI.Device.cppm` has no `Extrinsic.Platform.*` import.
- [x] `src/graphics/rhi/CMakeLists.txt` has no `ExtrinsicPlatform` link edge.
- [x] `tools/repo/check_layering.py --root src --strict` passes — 771 files scanned, 0 violations.
- [x] Runtime remains the only promoted layer that knows both platform and concrete graphics backend selection.
- [x] CPU-supported CI remains green. *(Local structural checks pass; default CPU gate deferred to the PR's pinned-clang-20 `pr-fast` row per `/AGENTS.md` §5.)*

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not make `graphics/rhi` depend on `platform` through another target name.
- Do not move platform abstractions into `core` just to satisfy the checker.
- Do not introduce Vulkan types into RHI public renderer APIs.
- Do not delete or bypass existing runtime fallback diagnostics.
