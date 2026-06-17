---
id: UI-018
theme: F
depends_on: [RORG-031F, UI-001, UI-002, UI-006, UI-007, UI-008, UI-013, UI-014, UI-015, UI-016, UI-017]
maturity_target: CPUContracted
---
# UI-018 — Sandbox menu-first UI defaults

## Completion
- Retired on 2026-06-17 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: the sandbox editor now starts menu-first. Top-level sandbox panels
  are exposed through the `View` menu and stay closed until the user toggles
  them open; existing PointCloud/Graph/Mesh domain windows remain
  menu-controlled and closed by default.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
  succeeded, and `ctest --test-dir build/ci --output-on-failure -R
  '^SandboxEditorUi\.' --timeout 120` passed 51/51 tests.

## Goal
- Make the promoted sandbox editor start with only its main menu bar visible; all sandbox editor windows open only when selected from the menu.

## Non-goals
- Reworking panel contents, command routing, or data models.
- Changing runtime, graphics, asset, or ECS ownership boundaries.
- Adding native docking/layout persistence or external UI preference storage.

## Context
- Owner: `runtime` editor UI, consumed by the `app` sandbox through `Runtime::IApplication`.
- Before this task, `Extrinsic.Runtime.SandboxEditorUi` drew core sandbox windows unconditionally, while domain windows already used menu-controlled open state.
- The requested behavior is a startup organization change: sandbox should expose menus first, with windows closed by default unless the user explicitly opens them.

## Slice plan
- **Slice A (this slice).** Add top-level sandbox panel open state, expose those panels through a main-menu item, keep the initial state closed, and add CPU/null ImGui contract coverage. This closes at `CPUContracted`; no `Operational` follow-up is owed because the behavior is backend-neutral and exercised through the existing ImGui adapter/test seam.

## Required changes
- [x] Promote this task to `tasks/active/` before implementation.
- [x] Add menu-controlled open state for top-level sandbox editor windows.
- [x] Keep all top-level and domain sandbox windows closed by default on first sandbox draw.
- [x] Preserve existing runtime-owned panel models and command surfaces.

## Tests
- [x] Add/update a CPU-only contract test proving default sandbox draw creates the menu bar but not panel windows.
- [x] Run the focused runtime contract test containing `SandboxEditorUi`.

## Docs
- [x] Update UI/runtime task notes if the startup behavior changes documented current state.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and retiring the task.

## Acceptance criteria
- [x] First sandbox UI frame exposes menu access without opening any sandbox editor panel/window by default.
- [x] Menu items can toggle the existing top-level sandbox editor windows.
- [x] Domain menu windows remain closed by default and continue to be menu-controlled.
- [x] Tests pass for the touched runtime UI contract.

## Verification
```bash
cmake --preset ci
python3 tools/agents/validate_tasks.py --root tasks --strict
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.DefaultDrawStartsWithOnlyMenuBarVisible$' --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making the sandbox app depend on graphics, platform, ECS, asset, or RHI layers directly.

## Maturity
- Target: `CPUContracted`.
- This slice closes menu-first startup behavior through CPU/null ImGui contract tests; no `Operational` follow-up is owed.
