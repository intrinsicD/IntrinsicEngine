# GRAPHICS-080 — Flip reference config + CI preset to enable promoted Vulkan

## Status

- Status: done (`Operational` on the local Vulkan-capable host; default CPU/null fallback and promoted `ci-vulkan` gates passed locally on 2026-05-18 as recorded below).
- Maturity: `Operational` for the promoted Vulkan reference-config/preset flip; canonical default-recipe triangle ownership remains explicitly staged to `GRAPHICS-076` + `GRAPHICS-081`.
- Owner/agent: Claude on `claude/inspect-engine-state-YRPpU`; GitHub Copilot final retirement on `main`.
- Branch: `main`.
- Completed: 2026-05-18.
- Commit/PR: pending current change.
- Slice 1 landed in this branch:
  - `ci-vulkan` configure + build preset (`CMakePresets.json`) inheriting from `ci` with `INTRINSIC_BUILD_SANDBOX=ON` and `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON`.
  - `CreateReferenceEngineConfig()` now sets `Render.EnablePromotedVulkanDevice = true` (`src/runtime/Runtime.Engine.cppm`).
  - Doc sync: `src/runtime/README.md`, `src/app/Sandbox/README.md`, `docs/architecture/graphics.md`, `tasks/backlog/rendering/README.md`, `tasks/backlog/README.md`, `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md`.
  - CI: new `.github/workflows/ci-vulkan.yml` row drives `cmake --preset ci-vulkan` + `ctest -L 'gpu' -L 'vulkan' -LE 'slow|flaky-quarantine'`. Default CPU gate workflows (`pr-fast`, `ci-linux-clang`) continue to use the `ci` preset unchanged. The `-LE 'slow|flaky-quarantine'` exclusion is a follow-up landed on a stacked branch (`claude/ci-vulkan-narrow-filter`) and is needed because `IntrinsicRuntimeTests` and its grouped entries are labelled `gpu vulkan slow` while HARDEN-041's label split is still pending; without it the intersection-form `-L 'gpu' -L 'vulkan'` filter pulls those broad runtime aggregates into the PR-time path.
- Acceptance criterion 3 (canonical visible triangle on a Vulkan-capable host) is staged: the initial form passed through [`GRAPHICS-033D`](GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md)'s `gpu;vulkan` visible-triangle smoke on 2026-05-17; the canonical form remains owned by `GRAPHICS-076` + `GRAPHICS-081`. Until those upstreams land, the runtime falls back to Null with the `VulkanRequestedButNotOperational` breadcrumb (truth-table row `RequestedButIncompleteGate`), matching this task's "On hosts without Vulkan support" criterion.
- Gate-input planning-gap fills are retired: [`tasks/done/GRAPHICS-033E`](GRAPHICS-033E-vulkan-operational-gate-barrier-validation.md) (gate 7, `BarrierValidationClean`) and [`tasks/done/GRAPHICS-033F`](GRAPHICS-033F-vulkan-operational-gate-public-service-reconciliation.md) (gate 8, `PublicServiceReconciled`) both landed and were retired on branch `claude/setup-agentic-workflow-2glkN`. The 9-step operational gate can now satisfy `{Operational, None}` on a Vulkan-capable host; CPU/null verification on this host (no Vulkan device) continues to fall back to Null with the same breadcrumb cadence. The parent `GRAPHICS-033` planning slice identified Impl-A/B/C/D but did not enumerate explicit children for these two gates.
- Final verification note (2026-05-18): full `ci` configure/build/default CTest gate passed; full `ci-vulkan` `ExtrinsicSandbox IntrinsicTests` build passed; both the documented intersection-form `ctest -L 'gpu' -L 'vulkan'` gate and the broader union sanity pass `ctest -L 'gpu|vulkan'` passed on this Vulkan-capable host. Strict task/doc/test-layout checks passed. Strict layering remains blocked only by pre-existing platform imports in graphics/RHI (`Extrinsic.Platform.Window` edges), not by this task's new changes.

## Goal
- Once `GRAPHICS-033C` lands and the operational gate is satisfiable, flip the runtime reference config and a `ci-vulkan` preset so the promoted Vulkan device is the default for `ExtrinsicSandbox` runs and `gpu;vulkan` smoke tests, while keeping the existing `ci` preset Vulkan-disabled for fast CPU/null verification.

## Non-goals
- No change to the GRAPHICS-033 nine-step gate logic.
- No change to validation-layer policy or fail-closed counters.
- No mutation of the existing `ci` preset's CPU-only behavior.
- No removal of the runtime fallback path; Null device remains the fallback when the operational gate fails.

## Context
- Status: not started.
- Owner/layer: `runtime` (config defaults), build system (presets).
- Today: `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN` is OFF by default in `CMakePresets.json`; `RenderConfig::EnablePromotedVulkanDevice` is `false` by default in `Core.Config.Render`; `CreateReferenceEngineConfig()` does not flip the runtime flag. The runtime selector at `Runtime.Engine.cpp:49–73` therefore always returns `Backends::Null::CreateNullDevice()`.
- This task is the runtime-config / build-preset switch; the actual Vulkan operational behavior is owned by `GRAPHICS-033A`/`B`/`C`/`D`. Without the upstream `GRAPHICS-033C`, flipping these flags only causes the runtime breadcrumb (`VulkanRequestedButNotOperational`) to fire — the device still falls back to Null per the truth table.

## Required changes
- [x] Add a `ci-vulkan` configure preset in `CMakePresets.json` that inherits from `ci` and adds `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON`.
- [x] Update `CreateReferenceEngineConfig()` (`Runtime.Engine.cppm:52`) to set `Render.EnablePromotedVulkanDevice = true`.
- [x] Document in `src/runtime/README.md` that the default reference config now requests promoted Vulkan but still falls back to Null per the GRAPHICS-033 truth table when the operational gate fails.
- [x] Update `src/app/Sandbox/README.md` with the recommended `cmake --preset ci-vulkan` invocation alongside the existing `ci` invocation.
- [x] Confirm CI workflows that drive the default CPU gate continue to use the `ci` preset (no Vulkan); add a separate workflow row that uses `ci-vulkan` for `gpu;vulkan` opt-in smoke (gated to runners that have a Vulkan device).

## Tests
- [x] `contract;runtime` test: with `EnablePromotedVulkanDevice = true` and a host without Vulkan support, the runtime resolves to Null, `VulkanFallbackToNullCount` increments by 1, and the `VulkanRequestedButNotOperational` breadcrumb fires once.
- [x] `contract;runtime` test: with `EnablePromotedVulkanDevice = false` (legacy callers), the runtime resolves to Null without breadcrumb noise.
- [x] CI smoke: the existing `ci` preset run continues to skip all `gpu;vulkan` fixtures by default.
- [x] CI smoke: the new `ci-vulkan` preset run selects fixtures co-labeled `gpu` **and** `vulkan` via `ctest -L 'gpu' -L 'vulkan'` (multiple `-L` filters require each regex to match at least one label, per `ctest --help`; this is the documented intersection form). The regex-alternation form `-L 'gpu|vulkan'` is **not** used because it is a union and would over-select: `IntrinsicPlatformGlfwSmokeTests` carries labels `integration platform glfw vulkan` (vulkan but not gpu) and would be pulled in by the union, expanding the gate beyond the promoted Vulkan GPU fixtures. The new `ci-vulkan` preset exits cleanly when no Vulkan device is present.

## Docs
- [x] Update `CMakePresets.json` documentation comments / `cmake/README.md` (if present) to enumerate the new preset.
- [x] Update `tasks/backlog/rendering/README.md` "Theme A" cross-link to mention this task as the flip-switch enabling end-to-end visible triangles.

## Acceptance criteria
- [x] `ci` preset behavior is unchanged (CPU-only verification stays fast).
- [x] `ci-vulkan` preset is configurable, builds, and routes the runtime through the promoted Vulkan path on Vulkan-capable hosts.
- [x] `ExtrinsicSandbox` started under the new preset on a Vulkan-capable host renders the GRAPHICS-029B reference triangle. The acceptance form is staged:
  - **Initial form** (immediately after `GRAPHICS-033C` lands, before `GRAPHICS-070..076` retire): the triangle renders through the `FrameRecipe::MinimalDebugSurface` recipe (the bootstrap scaffold authored by `GRAPHICS-032A..D`). This validates the Vulkan operational gate and the preset/config flip in isolation.
  - **Canonical form** (after `GRAPHICS-070..076` retire and `GRAPHICS-081` deletes the scaffold): the triangle renders through `BuildDefaultFrameRecipe`'s canonical `Pass.Forward.Surface` → `Pass.Present` lane. The `GRAPHICS-076` task is responsible for porting the visible-triangle acceptance to the default recipe, and `GRAPHICS-081` is responsible for deleting the minimal-recipe form. Once both retire, this task's acceptance criterion is read against the canonical form only.
- [x] On hosts without Vulkan support, runtime gracefully falls back to Null with the documented breadcrumb.

## Verification
```bash
# Default CPU gate stays fast and Vulkan-skipping:
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Promoted Vulkan gate (run on hosts with Vulkan):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120

python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing this preset/flip with new pass bodies or new diagnostics surfaces.
- Aborting startup on fallback to Null.
- Removing fail-closed counters.
- Changing `INTRINSIC_PLATFORM_BACKEND` defaults (the platform backend selector remains independent).

## Verification log
- 2026-05-18: `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` passed.
- 2026-05-18: `cmake --build --preset ci --target IntrinsicTests` passed.
- 2026-05-18: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed: 2010/2010.
- 2026-05-18: `cmake --preset ci-vulkan -DINTRINSIC_OFFLINE_DEPS=ON` passed.
- 2026-05-18: `cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicTests` passed.
- 2026-05-18: `ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120` passed: 1240/1240.
- 2026-05-18: `ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 120` passed: 1241/1241 (broader union sanity pass including `vulkan`-only platform smoke).
- 2026-05-18: `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` refreshed 434 modules after render-graph module import-order fix.
- 2026-05-18: `python3 tools/agents/check_task_policy.py --root . --strict`, `python3 tools/docs/check_doc_links.py --root .`, and `python3 tools/repo/check_test_layout.py --root . --strict` passed.
- 2026-05-18: `python3 tools/repo/check_layering.py --root src --strict` failed on pre-existing graphics/RHI -> platform window edges (`Extrinsic.Platform.Window`, `ExtrinsicPlatform`); no new violations were introduced by this task's changes.
