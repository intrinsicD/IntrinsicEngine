# LEGACY-003 — Delete `src/legacy/Apps/`

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `Retired`.
- Summary: The legacy Sandbox leaf application under `src/legacy/Apps/` is
  deleted, its top-level CMake source-root and `add_subdirectory` wiring are
  gone, and the `src/legacy/Apps/**` layering allowlist rows are removed.

## Goal
- [x] Remove the `src/legacy/Apps/` subtree (2 files: `Sandbox/main.cpp` and
      `Sandbox/CMakeLists.txt`) as a single mechanical commit, retire its
      `add_subdirectory(${INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT})` entry and the
      `INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT` assignment from the top-level CMake
      graph, drop any layering-allowlist rows granted only for this subtree, and
      regenerate the module inventory.

## Non-goals
- Do not delete any other `src/legacy/` subtree under cover of this task.
- Do not introduce new promoted code paths in this commit; the promoted
  `ExtrinsicSandbox` entry under `src/app/Sandbox/` (composing `src/platform/`
  and `src/runtime/`) must already be the canonical sandbox before this runs.
- Do not mix this deletion with any semantic refactor or feature work.
- Do not change `INTRINSIC_HEADLESS_NO_GLFW` or `INTRINSIC_BUILD_SANDBOX`
  defaults.
- Do not promote this task to `tasks/active/` before the prerequisite gate
  exits 0.

## Context
- Owning subsystem/layer: `src/legacy/Apps/` removal; promoted owners
  `src/app/Sandbox/` (UI shell entry) and `src/platform/` (window/input ports).
- Driver: legacy-retirement program seeded by
  [`LEGACY-002`](../backlog/architecture/LEGACY-002-seed-src-legacy-retirement-backlog.md) under
  [`ARCH-004`](ARCH-004-legacy-retirement-first-deletion-target.md);
  structural template is
  [`LEGACY-001`](../backlog/architecture/LEGACY-001-delete-src-legacy-interface.md).
- Subtree size: 2 files. The legacy `Sandbox` target is a pure leaf binary —
  it **exports no module** and is imported by nothing, so the consumer
  relationship is inverted relative to the other legacy subtrees: the legacy
  Sandbox `main.cpp` is one of the largest *consumers* of legacy `Graphics`,
  `RHI`, `Runtime.*`, and `Runtime.EditorUI`. Deleting it therefore unblocks
  several downstream subtree retirements rather than being blocked by them.
- Promoted equivalent: `src/app/Sandbox/` (`Extrinsic.Sandbox`) wired in
  `CMakeLists.txt` under `if(NOT INTRINSIC_HEADLESS_NO_GLFW)`.
- CMake references: `INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT` (`CMakeLists.txt`
  ~L237) and `add_subdirectory(${INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT})` under
  `if(INTRINSIC_BUILD_SANDBOX AND NOT INTRINSIC_HEADLESS_NO_GLFW)` (~L286–288).
- Prerequisite verified before retirement: `ExtrinsicSandbox` is the canonical
  app (`RUNTIME-095` retired), and no test/tool/source consumer depends on the
  legacy Sandbox target.

## Required changes
- [x] (Prerequisite, verified before promotion to `tasks/active/`) Run the
      consumer-grep gate in Verification and confirm it exits 0 with
      `OK: no external consumers ...`. Record the empty output in the commit
      message as evidence.
- [x] Delete the `src/legacy/Apps/` subtree (both files).
- [x] Remove the `add_subdirectory(${INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT})`
      line and the `INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT` assignment from
      `CMakeLists.txt`.
- [x] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key
      begins with `src/legacy/Apps/` (if any remain after the deletion).
- [x] Regenerate `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [x] Strike `Apps`/legacy-Sandbox mentions from
      [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md)
      where they referenced the now-deleted subtree, and record the retirement.
- [x] Check the subtree's row done in
      [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md)
      Sequencing.

## Tests
- [x] No new tests; this is a removal.
- [x] Default CPU gate stays green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [x] `python3 tools/repo/check_layering.py --root src --strict` passes and the
      allowlist row count drops by exactly the count of removed rows (zero if
      none).

## Docs
- [x] `docs/api/generated/module_inventory.md` regenerated.
- [x] `docs/migration/nonlegacy-parity-matrix.md` updated.
- [x] `docs/migration/legacy-retirement.md` Sequencing row recorded.

## Acceptance criteria
- [x] `src/legacy/Apps/` no longer exists.
- [x] `CMakeLists.txt` no longer references the removed subtree.
- [x] Allowlist row count does not increase; rows whose key begins with
      `src/legacy/Apps/` are removed.
- [x] Default CPU gate is green.
- [x] Commit is mechanical only (no behavior change elsewhere).

## Verification
```bash
# Gate: fail loudly if any test/tool/doc consumer of the legacy Sandbox remains
# outside the doomed subtree and outside the retirement-tracking files.
# `git grep` exits 0 on match, so invert it to block promotion.
if git grep -nE 'legacy/Apps/Sandbox|INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT' \
       -- 'tests/**' 'tools/**' 'src/**' \
       ':!src/legacy/Apps/**' ':!CMakeLists.txt'; then
    echo "ERROR: legacy Sandbox consumers remain outside the doomed subtree." >&2
    echo "       LEGACY-003 prerequisite NOT satisfied; do not promote to tasks/active/." >&2
    exit 1
fi
echo "OK: no external consumers of src/legacy/Apps/ remain."

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
  - `if git grep -nE 'legacy/Apps/Sandbox|INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT' ...; then ...; fi` — `OK: no external consumers of src/legacy/Apps/ remain.`
  - `if git grep -nE '^\s*(export\s+)?import\s+Runtime\.EditorUI\b' ...; then ...; fi` — `OK: no external consumers of src/legacy/EditorUI/ remain.`
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — inventory regenerated; no output diff.
  - `cmake --preset ci` — configure succeeds with the preset toolchain.
  - `cmake --build --preset ci --target IntrinsicTests` — builds clean.
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 2828/2828 passed.
  - `python3 tools/repo/check_layering.py --root src --strict` — no violations; allowlist entries dropped to 72.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — no findings.
  - `python3 tools/agents/validate_tasks.py --root tasks --strict` — no findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — no findings.
  - `python3 tools/agents/check_task_state_links.py --root . --strict` — no findings.
  - `python3 tools/docs/check_doc_links.py --root .` — no broken links.
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — docs sync rules satisfied.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check` — inventory up to date.
  - `tools/ci/run_clean_workshop_review.sh . --strict` — automated rows pass; manual rows 3-6 are `n/a` because this task deletes a legacy leaf app without adding public APIs, renderer members, passes, or frame-recipe dependencies.
  - `git diff --check` — no whitespace errors.

## Forbidden changes
- Mixing mechanical deletion with semantic refactors.
- Adding new code under `src/app/` or `src/platform/` to "replace" the deleted
  files in this same commit.
- Granting new allowlist rows under cover of this deletion.
- Removing more than the `Apps/` subtree.
- Promoting this task to `tasks/active/` before the consumer-grep prerequisite
  is satisfied.
