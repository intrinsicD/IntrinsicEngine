---
id: GRAPHICS-100
theme: B
depends_on: [GRAPHICS-099]
maturity_target: CPUContracted
---
# GRAPHICS-100 — Minimal current-renderer contract adapter

## Completion
- Retired on 2026-06-24 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Added `Extrinsic.Graphics.CurrentRendererContractAdapter`, a
  graphics-only CPU/null adapter that builds the current promoted renderer
  descriptor, immutable `RenderFrameInput` / `RenderWorld` snapshot envelopes,
  binding intents for existing renderer data lanes, the default frame-recipe
  descriptor, view/output recipes, and compatibility diagnostics through the
  `GRAPHICS-099` contract vocabulary. The adapter is data-only and does not
  change renderer execution, Vulkan, shaders, runtime extraction, UI, or asset
  service behavior.
- Evidence: `cmake --preset ci`; `cmake --build --preset ci --target
  IntrinsicTests -- -j16`; focused
  `RendererContract|RendererFrameLifecycle|RenderWorld|RenderExtraction` CTest
  filter; full CPU-supported CTest gate; structural validators listed in
  Verification.

## Goal
- Adapt the current renderer path to populate and consume the `GRAPHICS-099`
  contracts minimally, preserving rendered output and current Vulkan behavior.

## Non-goals
- No loadable recipe files or UI editing.
- No new visibility, lighting, or view/output behavior beyond default adapter
  population.
- No backend command or shader changes except those strictly required to keep
  existing behavior compiling.

## Context
- Owning subsystem/layer: graphics renderer plus runtime wiring seams only where
  runtime already prepares renderer input. Runtime remains the owner of live ECS
  and extraction; graphics consumes immutable prepared data.
- This is the first operational consumer of the contract vocabulary, but it is
  intentionally behavior-preserving.

## Required changes
- [x] Populate a default `RendererDescriptor` for the existing promoted
      renderer.
- [x] Build a default scoped snapshot envelope from the existing render-world or
      frame-input data without changing extraction behavior.
- [x] Map existing material, normal, color, texture, and visualization inputs to
      binding intents where equivalent data already exists.
- [x] Attach a default view/output recipe that describes the existing window or
      offscreen output path.
- [x] Surface compatibility diagnostics without blocking currently valid
      frames.

## Tests
- [x] Add contract/integration tests proving the current renderer descriptor is
      compatible with the default snapshot and view/output recipe.
- [x] Add regression coverage that existing default CPU-supported render
      lifecycle tests still pass.

## Docs
- [x] Update `src/graphics/renderer/README.md` with the adapter boundary.
- [x] Cross-link this task from `tasks/backlog/rendering/README.md`.

## Acceptance criteria
- [x] Existing renderer behavior is preserved while descriptor/snapshot/recipe
      metadata is populated.
- [x] Compatibility diagnostics are deterministic and test-covered.
- [x] No live ECS, platform, or asset-service dependencies are introduced into
      graphics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'RendererContract|RendererFrameLifecycle|RenderWorld|RenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root .
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing visual output intentionally.
- Implementing loadable recipe configuration or UI controls.

## Maturity
- Target: `CPUContracted`. This task proves the existing path can populate and
  consume the new contracts without behavior change.
- `Operational` owned by `GRAPHICS-103`.
