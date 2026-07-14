---
id: GRAPHICS-101
theme: B
depends_on: [GRAPHICS-099]
maturity_target: CPUContracted
---
# GRAPHICS-101 — Loadable rendering recipe config schema and validation

## Goal
- Add loadable file-backed schemas and fast validation for optional rendering
  recipe configuration, constrained to renderer-declared extension slots.

## Non-goals
- No UI editing workflow (owned by `UI-023`).
- No Vulkan, shader, or backend behavior changes.
- No execution of shared visibility or lighting recipes (owned by
  `GRAPHICS-102`).
- No mutation of project data from recipe activation.

## Context
- Owning subsystem/layer: graphics renderer/config contract code, with runtime
  activation integration deferred unless an existing lower-level config seam is
  already available.
- Accepted design: renderer frame-recipe cores are fixed; loadable recipes may
  only configure optional slots declared by renderer descriptors.

## Required changes
- [x] Define a versioned on-disk schema for renderer optional recipe slots,
      shared recipe descriptors, view/output recipes, and binding overrides.
- [x] Add a parser/loader that produces the `GRAPHICS-099` contract types
      without side effects.
- [x] Add validation that rejects unknown slots, unsupported capabilities,
      unsafe bindings, invalid defaults, and attempts to modify fixed renderer
      cores before activation.
- [x] Add diagnostics that distinguish invalid, unsupported, stale, degraded,
      and fallback-applied recipe states.
- [x] Add a preview/dry-run API surface suitable for agent-authored edits.

## Tests
- [x] Add parser and validator tests for valid recipes, unknown slots, invalid
      capability requirements, bad binding domains, version mismatch, and
      fallback diagnostics.
- [x] Add tests proving validation is side-effect free and fast enough for
      interactive activation checks.

## Docs
- [x] Document the recipe file shape and validation states.
- [x] Cross-link this task from `tasks/backlog/rendering/README.md`.

## Acceptance criteria
- [x] Loadable recipe configs are parsed into contract values and validated
      without mutating active renderer state.
- [x] Unsupported or unsafe config fails closed with precise diagnostics.
- [x] The fixed renderer recipe core cannot be replaced by config data.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'RenderRecipeConfig|RecipeValidation|ViewOutputRecipe' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Allowing arbitrary pass graph nodes or shader code through recipe files.
- Activating configs without validation.

## Maturity
- Target: `CPUContracted`. Recipe config parsing and validation are CPU-only.
- `Operational` owned by `GRAPHICS-103` for backend use; `UI-023` owns UI
  activation.

## Completion
- Retired on 2026-06-24 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Added `Extrinsic.Graphics.RenderRecipeConfig`, a CPU-only JSON schema,
  file loader, dry-run preview API, and validator that overlays configs onto a
  caller-provided `RenderRecipeConfigContext` without mutating renderer state.
- Validation rejects unknown renderer slots, unsupported capability strings,
  runtime/generated/unknown binding domains, required-binding overrides,
  invalid defaults payloads, and fixed-core replacement attempts before any
  activation path exists.
- Diagnostics distinguish `Invalid`, `Unsupported`, `Stale`, `Degraded`, and
  `FallbackApplied` states, while contract diagnostics remain available through
  `RenderingContractValidationResult`.
- Added graphics contract tests in
  `tests/contract/graphics/Test.RenderRecipeConfig.cpp` for valid string/file
  loads, failure states, fallback diagnostics, side-effect-free preview, and an
  interactive dry-run bound.
- Evidence: `cmake --preset ci`; `cmake --build --preset ci --target
  IntrinsicTests -- -j16`; focused
  `RenderRecipeConfig|RecipeValidation|ViewOutputRecipe` CTest filter;
  structural validators listed in Verification.
