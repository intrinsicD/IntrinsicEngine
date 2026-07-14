---
id: GRAPHICS-099
theme: B
depends_on: [GRAPHICS-098]
maturity_target: CPUContracted
---
# GRAPHICS-099 — Rendering contract foundation

## Completion
- Retired on 2026-06-24 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Added `Extrinsic.Graphics.RenderingContract`, a CPU-only public
  renderer/snapshot/recipe contract module with descriptor, snapshot envelope,
  binding intent, recipe slot, view/output, render artifact, deterministic
  diagnostic, and fail-closed validation DTOs. Added graphics contract tests
  proving the valid contract path and invalid descriptor, snapshot, binding,
  recipe-slot, view/output, and artifact-output cases without changing renderer
  execution, Vulkan, shaders, runtime, UI, or loadable-file behavior.
- Evidence: `cmake --preset ci`; `cmake --build --preset ci --target
  IntrinsicTests -- -j16`; focused `RenderingContract|RendererDescriptor|
  SnapshotEnvelope|BindingIntent|ViewOutputRecipe|RenderArtifact` CTest filter;
  full CPU-supported CTest gate; structural validators listed in Verification.

## Goal
- Add the CPU-only public rendering contract foundation for the accepted
  renderer/snapshot/recipe architecture without changing current renderer,
  Vulkan, UI, or loadable-file behavior.

## Non-goals
- No Vulkan, shader, RHI backend, or render-output behavior changes.
- No loadable recipe files, schema parser, or config activation workflow
  (owned by `GRAPHICS-101`).
- No current renderer extraction/submit rewiring (owned by `GRAPHICS-100`).
- No UI editing surface (owned by `UI-023`).
- No shared visibility, grouping, lighting, or environment recipe execution
  (owned by `GRAPHICS-102`).
- No runtime render-artifact registry or publish/apply workflow (owned by
  `RUNTIME-127`).

## Context
- Owning subsystem/layer: `src/graphics/renderer` for public renderer-facing
  contracts that import only allowed lower graphics/core types; tests live under
  graphics contract/unit coverage. Runtime integration is deliberately deferred.
- The accepted design says runtime pairs a renderer with a compatible scoped
  snapshot and view/output recipe. Renderers consume the engine render graph and
  fixed frame-recipe core, while loadable optional recipe parts are constrained
  to declared extension slots.
- This slice creates the stable data vocabulary first: renderer descriptors,
  scoped snapshot envelopes, binding intents, shared recipe descriptors,
  view/output recipes, render artifact metadata, validation helpers, and
  diagnostics. It must be useful to tests before any backend behavior changes.

## Required changes
- [x] Add a graphics renderer contract module for `RendererDescriptor`:
      renderer id, purpose, supported snapshot scopes, update modes,
      required/optional data categories, capabilities, outputs, and fallback
      policy.
- [x] Add scoped `SnapshotEnvelope` types: identity, kind, scope,
      producer/consumer renderer, source revisions, dependency hashes,
      validation state, stale/missing/generated/degraded flags, lifetime/cache
      policy, diagnostics, and replay/export metadata.
- [x] Add renderer-independent `BindingIntent` / `BindingSet` descriptors for
      semantic role, source domain, source identity/revision, value type/format,
      required/optional status, fallback policy, color space, units/range, and
      consumer role/pass/lens.
- [x] Add shared recipe descriptors for fixed-core renderer recipes and declared
      optional extension slots: stable slot name, schema id, defaults,
      capability requirements, allowed data bindings, validation rules,
      diagnostics, and fallback behavior.
- [x] Add shared view/output recipe descriptors covering camera/non-camera view
      data, viewport size, render scale, formats, target type, capture/readback
      needs, interactive/headless mode, and declared outputs.
- [x] Add `RenderArtifactMetadata` describing renderer, snapshot, view/output
      recipe, source revisions, status, diagnostics, lifetime, and artifact
      purpose.
- [x] Add fail-closed validation helpers for descriptor/snapshot/recipe
      compatibility and diagnostics aggregation.

## Tests
- [x] Add CPU-only graphics contract tests covering valid and invalid renderer
      descriptors, snapshot envelope compatibility, binding intent validation,
      optional recipe-slot rejection, view/output compatibility, and render
      artifact metadata lifecycle classification.
- [x] Cover unknown recipe slots, unsupported capabilities, missing required
      binding intents, stale/degraded snapshot flags, and undeclared output
      rejection.
- [x] Keep all new tests free of GPU, Vulkan, runtime, ECS, platform, and asset
      service dependencies.

## Docs
- [x] Update `src/graphics/renderer/README.md` with the contract vocabulary and
      the contract-first implementation boundary.
- [x] Update `tasks/backlog/rendering/README.md` to list `GRAPHICS-099` and its
      follow-up sequence.
- [x] Regenerate `docs/api/generated/module_inventory.md` if new module
      surfaces are added.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening this task and the
      follow-ups.

## Acceptance criteria
- [x] The contract types compile as promoted graphics/renderer APIs without
      importing ECS, runtime, platform, live asset services, or Vulkan types.
- [x] Validation helpers return deterministic diagnostics and fail closed for
      incompatible renderer/snapshot/recipe/output combinations.
- [x] CPU-only contract tests prove the public contracts independently of
      backend behavior.
- [x] No existing renderer output, Vulkan path, UI, or runtime extraction
      behavior changes in this task.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'RenderingContract|RendererDescriptor|SnapshotEnvelope|BindingIntent|ViewOutputRecipe|RenderArtifact' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing Vulkan, shader, swapchain, RHI backend, or current rendered image
  behavior.
- Adding runtime, ECS, platform, live asset service, or Vulkan dependencies to
  the public graphics/renderer contract module.

## Maturity
- Target: `CPUContracted`. This slice closes the public CPU/null contract only.
- `Operational` owned by `GRAPHICS-103`; `GRAPHICS-100` owns the first
  behavior-preserving current-renderer adapter.
