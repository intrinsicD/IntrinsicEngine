---
id: GRAPHICS-103
theme: B
depends_on: [GRAPHICS-100, GRAPHICS-102, RUNTIME-127]
maturity_target: Operational
---
# GRAPHICS-103 — Vulkan render-graph contract integration

## Goal
- Integrate the contract-first renderer/snapshot/recipe/artifact model with the
  promoted Vulkan/render-graph path and prove it with opt-in backend evidence.

## Non-goals
- No new public contract vocabulary (owned by `GRAPHICS-099`).
- No loadable config schema work (owned by `GRAPHICS-101`).
- No UI editing workflow (owned by `UI-023`).
- No unrelated renderer feature expansion.

## Context
- Owning subsystem/layer: graphics renderer/RHI/Vulkan integration, preserving
  the existing boundaries: no live ECS/runtime/platform imports in promoted
  graphics or Vulkan APIs, and no `Vk*` types through renderer/RHI public
  surfaces.
- This task is the backend operational proof after the CPU-only contract,
  current-renderer adapter, shared recipe execution, and runtime artifact
  publication tasks exist.

## Required changes
- [x] Route the promoted Vulkan-capable renderer through compatible descriptor,
      snapshot envelope, binding intent, shared recipe, view/output, and artifact
      metadata paths.
- [x] Map shared visibility/grouping and lighting/environment products into
      existing render graph passes without changing unsupported renderer cores.
- [x] Publish declared render outputs through runtime artifact metadata where
      the runtime integration seam exists.
- [x] Add backend diagnostics for unsupported recipe products, missing outputs,
      degraded fallbacks, and artifact publication failures.

## Tests
- [x] Add CPU contract regression tests for backend-agnostic graph integration.
- [x] Add opt-in `gpu;vulkan` smoke coverage proving at least one declared
      view/output recipe produces a non-empty image/artifact through the
      contract path.
- [x] Keep default CPU-supported CTest gate green.

## Docs
- [x] Update graphics renderer docs with the operational Vulkan integration
      boundary.
- [x] Refresh generated module inventory if public module surfaces change.
- [x] Cross-link this task from `tasks/backlog/rendering/README.md`.

## Acceptance criteria
- [x] The promoted Vulkan path consumes contract metadata without bypassing
      renderer/snapshot/recipe compatibility checks.
- [x] Opt-in backend evidence proves a declared output artifact is produced.
- [x] Default CPU gate remains green and Vulkan-specific checks remain opt-in.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'RenderContract|RenderGraph|RendererFrameLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests -- -j16
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RenderContract|RenderArtifact|Vulkan' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing ECS, runtime, platform, live asset services, or Vulkan types through
  promoted graphics public contracts.
- Treating CPU-only contract coverage as Vulkan operational proof.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; CPU-supported checks remain
  required everywhere else.

## Completion
- Retired on 2026-06-24 at maturity `Operational`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Integrated the current renderer with contract-first descriptor,
  scoped snapshot, binding intent, shared recipe, view/output, and declared
  artifact metadata evaluation inside the renderer frame lifecycle. The path
  fail-closes before render-graph execution when compatibility or artifact
  metadata validation fails, records deterministic unsupported-product,
  missing-output, degraded-fallback, and artifact-publication diagnostics, and
  finalizes declared artifact metadata from render-graph execution/readback
  outcomes while leaving runtime-owned artifact publication outside graphics.
- The opt-in Vulkan smoke now proves that a declared view/output recipe can
  produce a non-empty readback-backed artifact through the contract path.
- Evidence:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests -- -j16`
  - `ctest --test-dir build/ci --output-on-failure -R 'RenderContract|RenderGraph|RendererFrameLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `cmake --preset ci-vulkan`
  - `cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests -- -j16`
  - `ctest --test-dir build/ci-vulkan --output-on-failure -R 'RenderContract|RenderArtifact|Vulkan' -L 'gpu' -L 'vulkan' --timeout 120`
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
  - `python3 tools/repo/check_layering.py --root src --strict`
  - `python3 tools/agents/check_task_policy.py --root . --strict`
  - `python3 tools/agents/validate_tasks.py --root tasks --strict`
  - `python3 tools/repo/check_test_layout.py --root . --strict`
  - `python3 tools/docs/check_doc_links.py --root .`
  - `python3 tools/agents/check_task_state_links.py --root .`
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  - `git diff --check`
