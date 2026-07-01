---
id: LEGACY-001
theme: F
depends_on: []
---
# LEGACY-001 — Delete `src/legacy/Interface/`

## Status
Retired on 2026-07-01 at maturity `Retired`. `src/legacy/Interface/` was removed after Runtime and Graphics consumed no remaining Interface modules. Legacy CMake wiring and allowlist rows are gone, and the module inventory was regenerated.
- PR/commit: this local retirement commit.


## Goal
- [x] Remove the four-file `src/legacy/Interface/` subtree (`Gui.cpp`, `Gui.cppm`, `Interface.cppm`, `CMakeLists.txt`) from the repository as a single mechanical commit, retire its `add_subdirectory` entry from the top-level CMake graph, and drop any layering-allowlist rows that were granted only for this subtree.
- [x] Land the first concrete deletion that retires part of `src/legacy/` from the engine, proving the legacy-retirement program documented in [`ARCH-004`](ARCH-004-legacy-retirement-first-deletion-target.md) is operational.

## Non-goals
- [x] Do not delete any other `src/legacy/` subtree under cover of this task.
- [x] Do not introduce new promoted code paths in this commit. Promoted equivalents (`Extrinsic.Platform.Window`, `Extrinsic.Platform.Input`, `Extrinsic.Sandbox`) must already exist and be in use before this deletion runs.
- [x] Do not mix this deletion with any semantic refactor or feature work.
- [x] Do not change `INTRINSIC_HEADLESS_NO_GLFW` or `INTRINSIC_BUILD_SANDBOX` defaults.

## Context
- Owning subsystem/layer: `src/legacy/Interface/` removal; downstream owners `src/platform/` (window/input ports) and `src/app/` (UI shell entry).
- Driver: [`ARCH-004 — Pin first legacy-deletion target and sequencing`](ARCH-004-legacy-retirement-first-deletion-target.md).
- Files in scope:
  - `src/legacy/Interface/CMakeLists.txt`
  - `src/legacy/Interface/Gui.cpp`
  - `src/legacy/Interface/Gui.cppm`
  - `src/legacy/Interface/Interface.cppm`
- Top-level CMake reference: `CMakeLists.txt` does not currently add `src/legacy/Interface/` directly (it is only built when `INTRINSIC_HEADLESS_NO_GLFW=OFF` via the legacy interface subdirectory). Verify before deletion. If it is referenced, drop the `add_subdirectory(...)` line.
- Layering allowlist (`tools/repo/layering_allowlist.yaml`) carries grandfathered entries for legacy modules. Drop only rows whose key path begins with `src/legacy/Interface/`.

## Required changes
- [x] (Prerequisite, verified before this task is moved to `tasks/active/`) Run the consumer-grep gate in the Verification section and confirm it exits 0 with `OK: no external consumers ...`. The gate must fail loudly when any match outside `src/legacy/Interface/**` is found; `git grep` exits 0 on match, so the verification block inverts that. Today (2026-06-18) the gate still fails across six legacy Graphics/Runtime consumer files. `LEGACY-018` retired the external `tests/contract/ui/Test_PanelRegistration.cpp` consumer, and the legacy Sandbox and legacy EditorUI consumers retired under `LEGACY-003` and `LEGACY-007`; remaining consumers must migrate to promoted platform/app entry points or retire with their owning subtrees. Record the empty grep output in the commit message as evidence the prerequisite passed.
- [x] Delete the four files under `src/legacy/Interface/`.
- [x] Remove the corresponding `add_subdirectory(${INTRINSIC_LEGACY_INTERFACE_SOURCE_ROOT})` line and the `INTRINSIC_LEGACY_INTERFACE_SOURCE_ROOT` variable assignment from `CMakeLists.txt`.
- [x] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key begins with `src/legacy/Interface/`.
- [x] Regenerate `docs/api/generated/module_inventory.md` via `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [x] Strike `Interface` mentions from [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) where they referenced the now-deleted subtree, and add a note that the subtree was retired per this task.
- [x] Cross-link this task from [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md) Sequencing section (added by `ARCH-004`).

## Tests
- [x] No new tests; this is a removal.
- [x] Default CPU gate must remain green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [x] Sanitizer workflow (`.github/workflows/ci-sanitizers.yml`) must remain green on touched scope.
- [x] `python3 tools/repo/check_layering.py --root src --strict` must pass and the allowlist row count must drop by exactly the count of rows removed.

## Docs
- [x] [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md) regenerated so legacy module count drops accordingly.
- [x] [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) updated to remove `Interface` references where the subtree no longer exists.
- [x] [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md) Sequencing section gets the first-deletion checkmark.

## Acceptance criteria
- [x] `src/legacy/Interface/` no longer exists in the repository.
- [x] `CMakeLists.txt` no longer references the removed subtree.
- [x] Layering allowlist row count drops by the count of removed rows; no allowlist rows added.
- [x] Default CPU gate is green.
- [x] `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check` reports the inventory matches the new state.
- [x] Commit is mechanical only (no behavior change in other files; no renames mixed in).

## Verification
```bash
# Gate: fail loudly if any consumer of `src/legacy/Interface/` remains
# outside the doomed subtree itself. `git grep` exits 0 on match, so the
# check must invert that to block promotion. Record the (empty) output
# in the commit message as evidence the prerequisite passed.
#
# Today (2026-06-18) this gate fails across six legacy-internal files:
# src/legacy/Graphics/* (RenderDriver, DebugView/ImGui passes) and
# src/legacy/Runtime/* (Engine, RenderOrchestrator). `LEGACY-018` retired the
# external test consumer, and the legacy Sandbox consumer retired with
# LEGACY-003. The gate stays failing until the remaining consumers migrate to
# promoted platform/app entry points or retire with their owning subtrees
# (LEGACY-008/LEGACY-010).
if git grep -nE 'import\s+Interface\b|Interface::GUI|#include\s*"Interface' \
       -- 'src/**' 'tests/**' ':!src/legacy/Interface/**'; then
    echo "ERROR: src/legacy/Interface/ consumers remain outside the doomed subtree." >&2
    echo "       LEGACY-001 prerequisite NOT satisfied; do not promote to tasks/active/." >&2
    exit 1
fi
echo "OK: no external consumers of src/legacy/Interface/ remain."

# Standard structural gates:
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

# Build + default CPU gate:
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Mixing mechanical deletion with semantic refactors.
- Adding new code under `src/platform/` or `src/app/` to "replace" the deleted files in this same commit.
- Granting new allowlist rows under cover of this deletion.
- Disabling tests or sanitizer workflows to make this commit green.
- Removing more than the `Interface/` subtree.
- Promoting this task to `tasks/active/` before the consumer-grep prerequisite is satisfied per `ARCH-004`.

## Maturity
- Target: `Retired` (mechanical deletion of the legacy subtree).
- No `Operational` follow-up is owed; promoted ownership lives in `src/platform/` (window/input ports) and `src/app/Sandbox` (UI shell). See `docs/migration/nonlegacy-parity-matrix.md` for the feature-by-feature map.
- The consumer-grep gate in Verification must exit 0 before this task is promoted to `tasks/active/`; after `LEGACY-018`, the remaining consumers are owned by `LEGACY-008`/`LEGACY-010` (legacy subtrees), not external tests.
