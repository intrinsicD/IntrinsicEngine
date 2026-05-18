# GRAPHICS-033D — Opt-in Vulkan visible-triangle smoke fixture

## Goal
Land the fourth GRAPHICS-033 implementation child: the opt-in
`gpu;vulkan` integration smoke that drives the GRAPHICS-032 minimal
debug surface recipe through a real Vulkan device + GLFW window +
surface + swapchain and asserts that `RHI::IDevice::IsOperational()`
returns `true` only after every GRAPHICS-033 gate prerequisite is met.
This is the contract-level proof that the GLFW + Vulkan path produces
a single visible frame end-to-end. The smoke is excluded from the
default CPU correctness gate per AGENTS.md §7 and runs only on hosts
that expose Vulkan and the required X11/Wayland headers.

## Non-goals
- No changes to the operational gate (GRAPHICS-033A), the diagnostics
  snapshot (GRAPHICS-033B), or the command-recording bodies
  (GRAPHICS-033C). This task strictly composes existing seams.
- No new shader variants, materials, framegraph passes, or recipes
  beyond GRAPHICS-031 / GRAPHICS-032.
- No new public APIs on the platform or graphics layers.
- No additional Vulkan extension growth beyond the GRAPHICS-033
  Decision 7 minimal recipe.
- No changes to default CTest labels or to the default CPU gate.
- The smoke does not become part of CI by default. It is opt-in via
  the `gpu;vulkan` CTest labels and runs only when a host supports
  Vulkan + GLFW. It must not regress the CPU-only correctness gate.
- No editor/ImGui integration in this smoke beyond what
  GRAPHICS-013CQ already specifies.

## Context
- Owning subsystem/layer: `tests/integration/graphics/` (test code);
  consumes seams from `src/platform`, `src/graphics/vulkan`,
  `src/graphics/renderer`, and `src/runtime`.
- Depends on:
  - [`GRAPHICS-033A` (done)](../../done/GRAPHICS-033A-vulkan-operational-status-evaluator.md)
    operational status seam.
  - [`GRAPHICS-033B`](GRAPHICS-033B-vulkan-operational-diagnostics-snapshot.md)
    diagnostics snapshot.
  - [`GRAPHICS-033C`](GRAPHICS-033C-vulkan-minimal-recipe-command-recording.md)
    minimal-recipe command recording bodies.
  - [`GRAPHICS-032` (done)](../../done/GRAPHICS-032-minimal-surface-present-command-path.md)
    recipe definition.
  - [`GRAPHICS-031` (done)](../../done/GRAPHICS-031-default-debug-surface-material.md)
    default debug surface material.
  - [`PLATFORM-003` (done)](../../done/PLATFORM-003-explicit-platform-backends.md)
    GLFW backend module.
- Existing baseline: `tests/gpu/Test.VulkanBootstrapSmoke.cpp` opens a
  GLFW window, creates a Vulkan device + surface + swapchain, and
  asserts `IsOperational() == false`. This new task does not modify
  that test; it adds a sibling that asserts the operational-true case
  on a host that meets the full gate.
- Default CPU-supported correctness gate (AGENTS.md §7) is
  `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.
  This smoke registers `gpu;vulkan` labels so it is excluded.

## Required changes
- [ ] New `tests/integration/graphics/Test.VulkanVisibleTriangleSmoke.cpp`
  (CTest labels `gpu;vulkan;graphics`). The fixture:
  1. Skips with an explicit reason when GLFW cannot initialize or when
     `EvaluateVulkanOperationalStatus()` returns a code other than
     `Operational` at the point where the gate is fully satisfied.
  2. Otherwise opens a GLFW window via `Platform::CreateWindow()`,
     initializes the promoted Vulkan device with
     `RenderConfig::EnablePromotedVulkanDevice = true`, and asserts
     `device.IsOperational() == true`.
  3. Drives one frame through the GRAPHICS-032 `MinimalDebugSurface`
     recipe (BeginFrame → record `Pass.Surface.MinimalDebug` and
     `Pass.Present.MinimalDebug` → EndFrame → Present) and asserts
     that BeginFrame and Present complete without producing a
     non-operational status drop, that no
     `VulkanRequestedButNotOperational` breadcrumb fires, and that
     the relevant `VulkanOperationalDiagnosticsSnapshot` counters do
     not increment.
  4. Asserts `GraphicsBackendDiagnostics` / `RenderGraphFrameStats`
     report exactly the expected pass executions for the minimal
     recipe (one surface, one present).
- [ ] Register the smoke in `tests/CMakeLists.txt` with labels
  `gpu;vulkan;graphics`. Keep timeouts under the 120s opt-in budget
  documented in `tests/README.md`.
- [ ] Add a documented opt-in invocation block in `tests/README.md` and in
  `src/graphics/vulkan/README.md` so contributors know how to run the
  fixture locally.

## Tests
- [ ] The smoke itself is the deliverable. It is excluded from the default
  CPU gate. CPU-only tests must continue to pass unchanged.
- [ ] Verification commands:
  - Default CPU gate (must still pass):
    `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
  - Opt-in GPU gate (host-dependent):
    `ctest --test-dir build/ci -L 'gpu|vulkan' --timeout 120`.

## Docs
- [ ] Update `tests/README.md` opt-in section: add the visible-triangle
  smoke entry and the host-requirements list (Vulkan 1.3-capable
  device, GLFW-compatible windowing, X11 or Wayland dev headers).
- [ ] Update `src/graphics/vulkan/README.md` to record that the GRAPHICS-032
  recipe is now exercised by a real-device smoke on supported hosts.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` Vulkan smoke row.
- [ ] No regeneration of `docs/api/generated/module_inventory.md` is
  required if no new module surface is exported.

## Acceptance criteria
- [ ] `tests/integration/graphics/Test.VulkanVisibleTriangleSmoke.cpp`
  exists, builds, registers under `gpu;vulkan;graphics`, and is
  excluded from the default CPU correctness gate.
- [ ] On a host that satisfies the full GRAPHICS-033 gate, the smoke
  passes: `IsOperational() == true`, one minimal-recipe frame is
  recorded and presented, no startup breadcrumb fires, and no
  Vulkan operational counters increment.
- [ ] On a host without Vulkan or without the required X11/Wayland
  headers, the smoke either is not built (CMake-gated) or skips with
  a clear reason; the CPU gate is unaffected.
- [ ] Default CPU gate remains green.
- [ ] Layering, test-layout, task-validator, and module-inventory checks
  pass.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

cmake --preset ci -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Modifying GRAPHICS-033A/B/C semantics from inside this task.
- Adding the smoke to the default CPU gate or removing the
  `gpu;vulkan` label.
- Introducing a new Vulkan extension dependency beyond the
  GRAPHICS-033 Decision 7 minimal recipe.
- Adding ImGui/editor integration to the smoke.
- Live ECS access from `src/graphics/vulkan/*` or from the test
  fixture beyond the existing GRAPHICS-029 reference-scene seam.
- Per-frame heap allocations or string formatting on the recording
  path.
