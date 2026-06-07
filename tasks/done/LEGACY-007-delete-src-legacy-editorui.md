# LEGACY-007 — Delete `src/legacy/EditorUI/`

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `Retired`.
- Summary: The legacy `Runtime.EditorUI` subtree is deleted, its top-level
  CMake source-root and `add_subdirectory` wiring are gone, the
  `src/legacy/EditorUI/**` layering allowlist rows are removed, and stale docs
  now point at the promoted `Extrinsic.Runtime.SandboxEditorUi` seam instead.

## Goal
- [x] Remove the `src/legacy/EditorUI/` subtree (8 files) as a single mechanical
      commit, retire its `add_subdirectory(${INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT})`
      entry and the `INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT` assignment from the
      top-level CMake graph, drop the layering-allowlist rows granted only for
      this subtree, and regenerate the module inventory.

## Non-goals
- Do not delete any other `src/legacy/` subtree under cover of this task.
- Do not introduce new promoted code paths in this commit; the promoted editor
  shell under `src/app/` (`UI-001`) must already carry the consumers.
- Do not mix this deletion with any semantic refactor or feature work.
- Do not promote this task to `tasks/active/` before the prerequisite gate
  exits 0.

## Context
- Owning subsystem/layer: `src/legacy/EditorUI/` removal; promoted owner
  `src/app/` (editor shell + panels, `UI-001`).
- Driver: legacy-retirement program seeded by
  [`LEGACY-002`](../backlog/architecture/LEGACY-002-seed-src-legacy-retirement-backlog.md) under
  [`ARCH-004`](ARCH-004-legacy-retirement-first-deletion-target.md);
  structural template is
  [`LEGACY-001`](../backlog/architecture/LEGACY-001-delete-src-legacy-interface.md). This is sequencing
  row 3 (originally tracked as the placeholder "EditorUI → LEGACY-003") in
  [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md).
- Subtree size: 8 files. Legacy module name (bare, non-`Extrinsic.`):
  `Runtime.EditorUI`.
- Promoted equivalent: `Extrinsic.Runtime.SandboxEditorUi` attached by
  `src/app/Sandbox/`, delivered by `UI-001` on the
  `RUNTIME-090`/`GRAPHICS-079` ImGui adapter/pass stack and extended by
  `UI-002`, `UI-003`, and `RUNTIME-098`.
- CMake references: `INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT` (`CMakeLists.txt`
  ~L235) and `add_subdirectory(${INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT})` under
  `if(NOT INTRINSIC_HEADLESS_NO_GLFW)` (~L276).
- Layering allowlist (`tools/repo/layering_allowlist.yaml`) carries
  grandfathered rows keyed under `src/legacy/EditorUI/`; drop only those.
- Prerequisite verified before retirement: `LEGACY-003` retired the legacy
  Sandbox consumer and the previous `tests/integration/app/Test_EditorUI.cpp`
  consumer migrated to promoted `SandboxEditorUi` contract coverage under
  `UI-003`; the consumer-grep gate exited 0.

## Required changes
- [x] (Prerequisite, verified before promotion to `tasks/active/`) Run the
      consumer-grep gate in Verification and confirm it exits 0 with
      `OK: no external consumers ...`. Record the empty output in the commit
      message.
- [x] Delete the `src/legacy/EditorUI/` subtree (8 files, including its
      `CMakeLists.txt`).
- [x] Remove the `add_subdirectory(${INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT})`
      line and the `INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT` assignment from
      `CMakeLists.txt`.
- [x] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key
      begins with `src/legacy/EditorUI/`.
- [x] Regenerate `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [x] Strike legacy `Runtime.EditorUI` mentions from
      [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md)
      where they referenced the now-deleted subtree, and record the retirement.
- [x] Check the subtree's row done in
      [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md)
      Sequencing.

## Tests
- [x] No new tests; this is a removal. The previous
      `tests/integration/app/Test_EditorUI.cpp` legacy consumer migrated under
      `UI-003`; keep this commit mechanical and do not add replacement promoted
      behavior here.
- [x] Default CPU gate stays green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [x] `python3 tools/repo/check_layering.py --root src --strict` passes and the
      allowlist row count drops by exactly the count of removed rows.

## Docs
- [x] `docs/api/generated/module_inventory.md` regenerated.
- [x] `docs/migration/nonlegacy-parity-matrix.md` updated.
- [x] `docs/migration/legacy-retirement.md` Sequencing row recorded.

## Acceptance criteria
- [x] `src/legacy/EditorUI/` no longer exists.
- [x] `CMakeLists.txt` no longer references the removed subtree.
- [x] Allowlist row count drops by the count of removed rows; no rows added.
- [x] Default CPU gate is green.
- [x] Commit is mechanical only.

## Verification
```bash
# Gate: fail loudly if any consumer of legacy Runtime.EditorUI remains outside
# the doomed subtree. `git grep` exits 0 on match, so invert it.
if git grep -nE '^\s*(export\s+)?import\s+Runtime\.EditorUI\b' \
       -- 'src/**' 'tests/**' ':!src/legacy/EditorUI/**'; then
    echo "ERROR: src/legacy/EditorUI/ consumers remain outside the doomed subtree." >&2
    echo "       LEGACY-007 prerequisite NOT satisfied; do not promote to tasks/active/." >&2
    exit 1
fi
echo "OK: no external consumers of src/legacy/EditorUI/ remain."

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Completion record
- Completed: 2026-06-07.
- Branch: `main`.
- Commit: this retirement commit.
- Maturity reached: `Retired`.
- Verification run in this session:
  - `if git grep -nE '^\s*(export\s+)?import\s+Runtime\.EditorUI\b' ...; then ...; fi`
    — `OK: no external consumers of src/legacy/EditorUI/ remain.`
  - `rg -n 'INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT|add_subdirectory\(\$\{INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT\}\)|file_glob: "src/legacy/EditorUI|Runtime\.EditorUI \| `src/legacy/EditorUI/Runtime.EditorUI.cppm`' CMakeLists.txt tools/repo/layering_allowlist.yaml docs/api/generated/module_inventory.md || true`
    — no live references in CMake, layering allowlist, or module inventory.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
    — inventory regenerated; `Runtime.EditorUI` removed and total modules are
    479.
  - `cmake --preset ci` — configure succeeds with the preset toolchain.
  - `cmake --build --preset ci --target IntrinsicTests` — builds clean after
    removing stale `IntrinsicEditorUI` test link dependencies.
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 2828/2828 passed.
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    violations; allowlist entries dropped to 63.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — no
    findings.
  - `python3 tools/agents/validate_tasks.py --root tasks --strict` — no
    findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — no
    findings.
  - `python3 tools/agents/check_task_state_links.py --root . --strict` — no
    findings.
  - `python3 tools/docs/check_doc_links.py --root .` — no broken links.
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
    — docs sync rules satisfied.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check`
    — inventory up to date.
  - `tools/ci/run_clean_workshop_review.sh . --strict` — automated rows pass;
    manual rows 3-6 are `n/a` because this task deletes a legacy subtree
    without adding public APIs, renderer members, passes, or frame-recipe
    dependencies.
  - `git diff --check` — no whitespace errors.

## Forbidden changes
- Mixing mechanical deletion with semantic refactors.
- Adding new code under `src/app/` to "replace" the deleted files in this same
  commit.
- Granting new allowlist rows under cover of this deletion.
- Removing more than the `EditorUI/` subtree.
- Promoting this task to `tasks/active/` before the consumer-grep prerequisite
  is satisfied.
