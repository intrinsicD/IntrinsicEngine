# LEGACY-001 — Delete `src/legacy/Interface/`

## Goal
- [ ] Remove the three-file `src/legacy/Interface/` subtree (`Gui.cpp`, `Gui.cppm`, `Interface.cppm`, `CMakeLists.txt`) from the repository as a single mechanical commit, retire its `add_subdirectory` entry from the top-level CMake graph, and drop any layering-allowlist rows that were granted only for this subtree.
- [ ] Land the first concrete deletion that retires part of `src/legacy/` from the engine, proving the legacy-retirement program documented in [`ARCH-004`](../../done/ARCH-004-legacy-retirement-first-deletion-target.md) is operational.

## Non-goals
- [ ] Do not delete any other `src/legacy/` subtree under cover of this task.
- [ ] Do not introduce new promoted code paths in this commit. Promoted equivalents (`Extrinsic.Platform.Window`, `Extrinsic.Platform.Input`, `Extrinsic.Sandbox`) must already exist and be in use before this deletion runs.
- [ ] Do not mix this deletion with any semantic refactor or feature work.
- [ ] Do not change `INTRINSIC_HEADLESS_NO_GLFW` or `INTRINSIC_BUILD_SANDBOX` defaults.

## Context
- Owning subsystem/layer: `src/legacy/Interface/` removal; downstream owners `src/platform/` (window/input ports) and `src/app/` (UI shell entry).
- Driver: [`ARCH-004 — Pin first legacy-deletion target and sequencing`](../../done/ARCH-004-legacy-retirement-first-deletion-target.md).
- Files in scope:
  - `src/legacy/Interface/CMakeLists.txt`
  - `src/legacy/Interface/Gui.cpp`
  - `src/legacy/Interface/Gui.cppm`
  - `src/legacy/Interface/Interface.cppm`
- Top-level CMake reference: `CMakeLists.txt` does not currently add `src/legacy/Interface/` directly (it is only built when `INTRINSIC_HEADLESS_NO_GLFW=OFF` via the legacy interface subdirectory). Verify before deletion. If it is referenced, drop the `add_subdirectory(...)` line.
- Layering allowlist (`tools/repo/layering_allowlist.yaml`) carries grandfathered entries for legacy modules. Drop only rows whose key path begins with `src/legacy/Interface/`.

## Required changes
- [ ] (Prerequisite, verified before this task is moved to `tasks/active/`) Run the consumer-grep gate in the Verification section and confirm it exits 0 with `OK: no external consumers ...`. The gate must fail loudly when any match outside `src/legacy/Interface/**` is found; `git grep` exits 0 on match, so the verification block inverts that. Today (2026-06-07) the gate still fails across remaining legacy graphics/runtime consumers and `tests/contract/ui/Test_PanelRegistration.cpp`. The legacy Sandbox and legacy EditorUI consumers retired under `LEGACY-003` and `LEGACY-007`; remaining consumers must migrate to the promoted platform/app entry points. Record the empty grep output in the commit message as evidence the prerequisite passed.
- [ ] Delete the four files under `src/legacy/Interface/`.
- [ ] Remove the corresponding `add_subdirectory(${INTRINSIC_LEGACY_INTERFACE_SOURCE_ROOT})` line and the `INTRINSIC_LEGACY_INTERFACE_SOURCE_ROOT` variable assignment from `CMakeLists.txt`.
- [ ] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key begins with `src/legacy/Interface/`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` via `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [ ] Strike `Interface` mentions from [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) where they referenced the now-deleted subtree, and add a note that the subtree was retired per this task.
- [ ] Cross-link this task from [`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md) Sequencing section (added by `ARCH-004`).

## Tests
- [ ] No new tests; this is a removal.
- [ ] Default CPU gate must remain green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [ ] Sanitizer workflow (`.github/workflows/ci-sanitizers.yml`) must remain green on touched scope.
- [ ] `python3 tools/repo/check_layering.py --root src --strict` must pass and the allowlist row count must drop by exactly the count of rows removed.

## Docs
- [ ] [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md) regenerated so legacy module count drops accordingly.
- [ ] [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) updated to remove `Interface` references where the subtree no longer exists.
- [ ] [`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md) Sequencing section gets the first-deletion checkmark.

## Acceptance criteria
- [ ] `src/legacy/Interface/` no longer exists in the repository.
- [ ] `CMakeLists.txt` no longer references the removed subtree.
- [ ] Layering allowlist row count drops by the count of removed rows; no allowlist rows added.
- [ ] Default CPU gate is green.
- [ ] `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check` reports the inventory matches the new state.
- [ ] Commit is mechanical only (no behavior change in other files; no renames mixed in).

## Verification
```bash
# Gate: fail loudly if any consumer of `src/legacy/Interface/` remains
# outside the doomed subtree itself. `git grep` exits 0 on match, so the
# check must invert that to block promotion. Record the (empty) output
# in the commit message as evidence the prerequisite passed.
#
# Today (2026-05-16) this gate fails: ~248 matches across files including
# src/legacy/Apps/Sandbox/main.cpp, src/legacy/Graphics/Graphics.RenderDriver.cpp,
# src/legacy/Runtime/*, and
# tests/contract/ui/Test_PanelRegistration.cpp. The gate stays failing
# until consumers migrate to the promoted platform/app entry points.
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
