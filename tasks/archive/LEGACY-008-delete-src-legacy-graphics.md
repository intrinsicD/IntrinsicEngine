---
id: LEGACY-008
theme: F
depends_on: []
---
# LEGACY-008 — Delete `src/legacy/Graphics/`

## Status
Retired on 2026-07-01 at maturity `Retired`. `src/legacy/Graphics/` was removed after Runtime, clearing its remaining legacy-internal consumers before upstream Interface/ECS/Asset/RHI/Core deletion. Legacy CMake wiring and allowlist rows are gone, and the module inventory was regenerated.
- PR/commit: this local retirement commit.


## Goal
- [x] Remove the `src/legacy/Graphics/` subtree (168 files) as a single
      mechanical commit, retire its
      `add_subdirectory(${INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT})` entry and the
      `INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT` assignment from the top-level CMake
      graph, drop the layering-allowlist rows granted only for this subtree, and
      regenerate the module inventory.

## Non-goals
- Do not delete any other `src/legacy/` subtree under cover of this task.
- Do not introduce new promoted code paths in this commit; the promoted
  `src/graphics/*` (`rhi`, `assets`, `renderer`, `framegraph`, `vulkan`)
  surfaces must already carry the consumers.
- Do not mix this deletion with any semantic refactor or feature work.
- Do not promote this task to `tasks/active/` before the prerequisite gate
  exits 0.

## Context
- Owning subsystem/layer: `src/legacy/Graphics/` removal; promoted owners
  `src/graphics/rhi/`, `src/graphics/assets/`, `src/graphics/renderer/`,
  `src/graphics/framegraph/`, `src/graphics/vulkan/`.
- Driver: legacy-retirement program seeded by
  [`LEGACY-002`](LEGACY-002-seed-src-legacy-retirement-backlog.md) under
  [`ARCH-004`](ARCH-004-legacy-retirement-first-deletion-target.md);
  structural template is
  [`LEGACY-001`](LEGACY-001-delete-src-legacy-interface.md).
- Subtree size: 168 files — the largest legacy subtree. Legacy module names
  (bare, non-`Extrinsic.`): the primary module `Graphics` plus `Graphics.*`
  (e.g. `Graphics.Camera`, `Graphics.Components`, `Graphics.ColorMapper`).
- Promoted equivalent: the `src/graphics/*` layer set produced by the
  `GRAPHICS-029..081` Theme A/B promotion chain plus the promoted Vulkan device.
- CMake references: `INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT` (`CMakeLists.txt`
  ~L233) and `add_subdirectory(${INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT})` under
  `if(NOT INTRINSIC_HEADLESS_NO_GLFW)` (~L274; `# needs Asset`).
- Layering allowlist (`tools/repo/layering_allowlist.yaml`) carries
  grandfathered rows keyed under `src/legacy/Graphics/`; drop only those.
- Prerequisite update (2026-06-18): the consumer-grep gate FAILS — legacy
  `Graphics`/`Graphics.*` modules are still imported by `src/legacy/Runtime/`
  and by 36 tests after `LEGACY-041` retired the legacy `Asset.Manager`
  async/cache/lease/clear compatibility test consumer. The legacy Sandbox
  consumer retired under `LEGACY-003`, and the legacy EditorUI consumer retired
  under `LEGACY-007`. Per
  [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md),
  this subtree's retirement is gated on the
  `GRAPHICS-033 + GRAPHICS-070..076 + GRAPHICS-081` chain (already retired) plus
  the migration of the remaining legacy consumers off `Graphics.*`.

## Required changes
- [x] (Prerequisite, verified before promotion to `tasks/active/`) Run the
      consumer-grep gate in Verification and confirm it exits 0 with
      `OK: no external consumers ...`. Record the empty output in the commit
      message.
- [x] Delete the `src/legacy/Graphics/` subtree (168 files, including its
      `CMakeLists.txt`).
- [x] Remove the `add_subdirectory(${INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT})`
      line and the `INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT` assignment from
      `CMakeLists.txt`.
- [x] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key
      begins with `src/legacy/Graphics/`.
- [x] Regenerate `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [x] Strike legacy `Graphics`/`Graphics.*` mentions from
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
      allowlist row count drops by exactly the count of removed rows.

## Docs
- [x] `docs/api/generated/module_inventory.md` regenerated.
- [x] `docs/migration/nonlegacy-parity-matrix.md` updated.
- [x] `docs/migration/legacy-retirement.md` Sequencing row recorded.

## Acceptance criteria
- [x] `src/legacy/Graphics/` no longer exists.
- [x] `CMakeLists.txt` no longer references the removed subtree.
- [x] Allowlist row count drops by the count of removed rows; no rows added.
- [x] Default CPU gate is green.
- [x] Commit is mechanical only.

## Verification
```bash
# Gate: fail loudly if any consumer of legacy Graphics/Graphics.* modules
# remains outside the doomed subtree. `git grep` exits 0 on match, so invert it.
# `import\s+Graphics\b` matches `import Graphics;` / `import Graphics.Camera;`
# but NOT the promoted `import Extrinsic.Graphics.*;`.
if git grep -nE '^\s*(export\s+)?import\s+Graphics\b' \
       -- 'src/**' 'tests/**' ':!src/legacy/Graphics/**'; then
    echo "ERROR: src/legacy/Graphics/ consumers remain outside the doomed subtree." >&2
    echo "       LEGACY-008 prerequisite NOT satisfied; do not promote to tasks/active/." >&2
    exit 1
fi
echo "OK: no external consumers of src/legacy/Graphics/ remain."

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Mixing mechanical deletion with semantic refactors.
- Adding new code under `src/graphics/*` to "replace" the deleted files in this
  same commit.
- Granting new allowlist rows under cover of this deletion.
- Removing more than the `Graphics/` subtree.
- Promoting this task to `tasks/active/` before the consumer-grep prerequisite
  is satisfied.

## Maturity
- Target: `Retired` (mechanical deletion of the legacy subtree; largest
  remaining subtree by file count).
- No `Operational` follow-up is owed by this deletion; promoted ownership
  lives in `src/graphics/*` (see `docs/migration/nonlegacy-parity-matrix.md`).
  Feature candidates that survived the value gate are retired by the
  `LEGACY-011` map and its children (`GRAPHICS-084..086`), not by this task.
- The consumer-grep gate in Verification must exit 0 before this task is
  promoted to `tasks/active/`; this deletion is typically coordinated with
  `LEGACY-009` (legacy RHI) and `LEGACY-010` (legacy Runtime) because the
  three subtrees import each other.
