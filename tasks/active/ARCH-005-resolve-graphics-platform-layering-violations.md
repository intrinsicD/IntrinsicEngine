# ARCH-005 — Resolve graphics/RHI platform layering violations

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
- [ ] Inventory all promoted `src/` imports and CMake links involving `Extrinsic.Platform.Window`, `ExtrinsicPlatform`, and graphics/RHI targets; confirm the four violations above are the complete strict-layering set.
- [ ] Design the smallest boundary seam that lets runtime compose a platform window with the selected graphics backend while preserving these ownership rules:
  - `graphics/rhi` remains platform-free.
  - `graphics/vulkan` remains platform-free and exposes no `Vk*` types through RHI/renderer APIs.
  - `platform` remains graphics-free.
  - `runtime` owns the cross-layer wiring.
- [ ] Remove `Extrinsic.Platform.Window` imports from `RHI.Device.cppm`, `Backends.Vulkan.Device.cppm`, and `Backends.Null.cpp`.
- [ ] Remove the `ExtrinsicPlatform` link edge from `src/graphics/rhi/CMakeLists.txt`.
- [ ] Update any runtime/backend construction code and tests affected by the new seam.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if any C++23 module surface changes.

## Tests
- [ ] Build the focused affected targets after the boundary refactor.
- [ ] Run the default CPU-supported correctness gate:
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- [ ] On a Vulkan-capable host, run the promoted Vulkan smoke gate:
  ```bash
  ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -LE 'slow|flaky-quarantine' --timeout 120
  ```
- [ ] Run the strict layering check and confirm it passes:
  ```bash
  python3 tools/repo/check_layering.py --root src --strict
  ```

## Docs
- [ ] Update `docs/architecture/graphics.md`, `docs/architecture/runtime.md`, or ADR material if the chosen seam changes public ownership/wiring policy.
- [ ] Update `tasks/backlog/architecture/RORG-036-layer-ownership-audit.md` only if this task resolves an audit finding or changes its follow-up list.
- [ ] Record final verification evidence in this task before moving it to `tasks/done/`.

## Acceptance criteria
- [ ] `python3 tools/repo/check_layering.py --root src --strict` reports zero violations.
- [ ] No graphics/RHI module imports `Extrinsic.Platform.Window` or links `ExtrinsicPlatform`.
- [ ] No platform module imports graphics/RHI/runtime to compensate for the removed edge.
- [ ] Runtime remains the composition owner for connecting the selected platform backend to the selected graphics backend.
- [ ] Existing Null and Vulkan fallback behavior remains covered by the default CPU gate and promoted Vulkan smoke gate.

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
