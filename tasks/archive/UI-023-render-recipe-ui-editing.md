---
id: UI-023
theme: B
depends_on: [GRAPHICS-101, RUNTIME-127]
maturity_target: CPUContracted
---
# UI-023 — Sandbox render recipe editing UI

## Goal
- Add a sandbox editor UI surface for inspecting and editing loadable rendering
  recipe options through runtime/graphics command seams without UI owning
  renderer state.

## Non-goals
- No renderer contract definitions (owned by `GRAPHICS-099`).
- No recipe parser/validator implementation (owned by `GRAPHICS-101`).
- No render artifact registry or publish/apply implementation (owned by
  `RUNTIME-127`).
- No Vulkan behavior changes.
- No direct UI mutation of project data or graphics backend state.

## Context
- Owning subsystem/layer: `src/runtime/Editor` UI model and command surface.
  App imports runtime only; UI sends commands to owning runtime/graphics seams.
- The UI must show clearly whether edits are inactive drafts, validated,
  rejected, debounced, canceled, previewed, activated, or unpublished.
- This task depends on `RUNTIME-127` because the current scope includes artifact
  lifetime choices and unpublished/published status.

## Required changes
- [x] Add editor models for renderer descriptors, available recipe slots,
      active recipe values, draft edits, validation diagnostics, preview status,
      and activation status.
- [x] Add sandbox UI controls for declared optional recipe slots, binding
      overrides, view/output recipes, and artifact lifetime choices.
- [x] Route edits through runtime-owned commands and validation calls; do not
      mutate graphics state directly.
- [x] Mark stale or invalid data with existing status color/tooltip patterns
      where applicable.

## Tests
- [x] Add runtime/editor UI model tests covering draft, validation failure,
      preview success/failure, activation, cancellation, and unchanged no-op
      behavior.
- [x] Keep UI tests headless and CPU-supported.

## Docs
- [x] Update runtime/editor README documentation for the render recipe editing
      flow.
- [x] Add this task to `tasks/backlog/ui/README.md`.

## Acceptance criteria
- [x] Users can inspect and edit only renderer-declared recipe options.
- [x] UI state is explicit about draft/validated/activated/canceled/debounced
      outcomes.
- [x] All mutations route through runtime-owned command seams.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RenderRecipe' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing graphics, platform, or ImGui details into lower runtime model code
  beyond established editor UI boundaries.
- Silently activating invalid recipe edits.

## Maturity
- Target: `CPUContracted`. The UI command/model contract is headless-tested.
- No `Operational` follow-up is owed by this UI model slice; visual workflow
  proof can be opened as a later value-gated UI smoke task if needed.

## Completion
- Retired on 2026-06-24 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Extended `Extrinsic.Runtime.SandboxEditorUi` with data-only render
  recipe editor models, draft/validation/preview/activation command DTOs, and
  artifact publish/apply command routing through runtime-owned state. The
  attached ImGui panel now exposes the current renderer descriptor, declared
  recipe slots, binding overrides, view/output recipe data, draft diagnostics,
  preview/activation state, and render artifact lifecycle rows without owning
  renderer state or mutating graphics/backend resources directly.
- Evidence:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests -- -j16`
  - `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RenderRecipe' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `python3 tools/repo/check_layering.py --root src --strict`
  - `python3 tools/agents/check_task_policy.py --root . --strict`
  - `python3 tools/agents/validate_tasks.py --root tasks --strict`
  - `python3 tools/docs/check_doc_links.py --root .`
  - `python3 tools/agents/check_task_state_links.py --root .`
  - `python3 tools/repo/check_test_layout.py --root . --strict`
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  - `git diff --check`
