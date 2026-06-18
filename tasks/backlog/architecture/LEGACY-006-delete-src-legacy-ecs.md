---
id: LEGACY-006
theme: F
depends_on: []
---
# LEGACY-006 — Delete `src/legacy/ECS/`

## Goal
- [ ] Remove the `src/legacy/ECS/` subtree (29 files) as a single mechanical
      commit, retire its `add_subdirectory(${INTRINSIC_LEGACY_ECS_SOURCE_ROOT})`
      entry and the `INTRINSIC_LEGACY_ECS_SOURCE_ROOT` assignment from the
      top-level CMake graph, drop the layering-allowlist rows granted only for
      this subtree, and regenerate the module inventory.

## Non-goals
- Do not delete any other `src/legacy/` subtree under cover of this task.
- Do not introduce new promoted code paths in this commit; the promoted
  `src/ecs/` (`Extrinsic.ECS`) surface must already carry the consumers.
- Do not mix this deletion with any semantic refactor or feature work.
- Do not promote this task to `tasks/active/` before the prerequisite gate
  exits 0.

## Context
- Owning subsystem/layer: `src/legacy/ECS/` removal; promoted owner
  `src/ecs/` (`Extrinsic.ECS`).
- Driver: legacy-retirement program seeded by
  [`LEGACY-002`](../../done/LEGACY-002-seed-src-legacy-retirement-backlog.md) under
  [`ARCH-004`](../../done/ARCH-004-legacy-retirement-first-deletion-target.md);
  structural template is
  [`LEGACY-001`](LEGACY-001-delete-src-legacy-interface.md).
- Subtree size: 29 files. Legacy module names (bare, non-`Extrinsic.`): the
  primary module `ECS` plus its partitions `ECS:Components`,
  `ECS:Components.*`, `ECS:ComponentForwardDecls`, etc.
- Promoted equivalent: `src/ecs/` (`Extrinsic.ECS`; the `HARDEN-060..068`
  promotion series).
- CMake references: `INTRINSIC_LEGACY_ECS_SOURCE_ROOT` (`CMakeLists.txt` ~L230)
  and `add_subdirectory(${INTRINSIC_LEGACY_ECS_SOURCE_ROOT})` (~L270,
  unconditional).
- Layering allowlist (`tools/repo/layering_allowlist.yaml`) carries
  grandfathered rows keyed under `src/legacy/ECS/`; drop only those.
- Prerequisite update (2026-06-18): the consumer-grep gate FAILS — legacy
  `ECS` is still imported by other legacy subtrees (`Graphics`, `Runtime`) and
  by 23 tests after `LEGACY-031` retired legacy ECS frame-graph systems
  compatibility coverage. The legacy Sandbox consumer retired under
  `LEGACY-003`; remaining consumers must migrate to `Extrinsic.ECS` or retire
  under `LEGACY-012` before promotion.

## Required changes
- [ ] (Prerequisite, verified before promotion to `tasks/active/`) Run the
      consumer-grep gate in Verification and confirm it exits 0 with
      `OK: no external consumers ...`. Record the empty output in the commit
      message.
- [ ] Delete the `src/legacy/ECS/` subtree (29 files, including its
      `CMakeLists.txt`).
- [ ] Remove the `add_subdirectory(${INTRINSIC_LEGACY_ECS_SOURCE_ROOT})` line
      and the `INTRINSIC_LEGACY_ECS_SOURCE_ROOT` assignment from `CMakeLists.txt`.
- [ ] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key
      begins with `src/legacy/ECS/`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [ ] Strike legacy `ECS` mentions from
      [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md)
      where they referenced the now-deleted subtree, and record the retirement.
- [ ] Check the subtree's row done in
      [`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md)
      Sequencing.

## Tests
- [ ] No new tests; this is a removal.
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [ ] `python3 tools/repo/check_layering.py --root src --strict` passes and the
      allowlist row count drops by exactly the count of removed rows.

## Docs
- [ ] `docs/api/generated/module_inventory.md` regenerated.
- [ ] `docs/migration/nonlegacy-parity-matrix.md` updated.
- [ ] `docs/migration/legacy-retirement.md` Sequencing row recorded.

## Acceptance criteria
- [ ] `src/legacy/ECS/` no longer exists.
- [ ] `CMakeLists.txt` no longer references the removed subtree.
- [ ] Allowlist row count drops by the count of removed rows; no rows added.
- [ ] Default CPU gate is green.
- [ ] Commit is mechanical only.

## Verification
```bash
# Gate: fail loudly if any consumer of the legacy ECS module remains outside
# the doomed subtree. `git grep` exits 0 on match, so invert it.
# `import\s+ECS\b` matches `import ECS;` / `import ECS:Components;` but NOT the
# promoted `import Extrinsic.ECS;`.
if git grep -nE '^\s*(export\s+)?import\s+ECS\b' \
       -- 'src/**' 'tests/**' ':!src/legacy/ECS/**'; then
    echo "ERROR: src/legacy/ECS/ consumers remain outside the doomed subtree." >&2
    echo "       LEGACY-006 prerequisite NOT satisfied; do not promote to tasks/active/." >&2
    exit 1
fi
echo "OK: no external consumers of src/legacy/ECS/ remain."

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
- Adding new code under `src/ecs/` to "replace" the deleted files in this same
  commit.
- Granting new allowlist rows under cover of this deletion.
- Removing more than the `ECS/` subtree.
- Promoting this task to `tasks/active/` before the consumer-grep prerequisite
  is satisfied.

## Maturity
- Target: `Retired` (mechanical deletion of the legacy subtree).
- No `Operational` follow-up is owed; promoted ownership lives in `src/ecs/`
  (scene/hierarchy/component parity retired under Theme D, `HARDEN-060..068`
  and `HARDEN-081`; see `docs/migration/nonlegacy-parity-matrix.md`).
- The consumer-grep gate in Verification must exit 0 before this task is
  promoted to `tasks/active/`; today the remaining consumers live in
  `src/legacy/Graphics/`, `src/legacy/Runtime/`, and compatibility tests owned
  by `LEGACY-012`.
