---
id: LEGACY-009
theme: F
depends_on: []
---
# LEGACY-009 — Delete `src/legacy/RHI/`

## Goal
- [ ] Remove the `src/legacy/RHI/` subtree (54 files) as a single mechanical
      commit, retire its `add_subdirectory(${INTRINSIC_LEGACY_RHI_SOURCE_ROOT})`
      entry and the `INTRINSIC_LEGACY_RHI_SOURCE_ROOT` assignment from the
      top-level CMake graph, drop the layering-allowlist rows granted only for
      this subtree, and regenerate the module inventory.

## Non-goals
- Do not delete any other `src/legacy/` subtree under cover of this task.
- Do not introduce new promoted code paths in this commit; the promoted
  `src/graphics/rhi/` (`Extrinsic.RHI.*`) surface must already carry the
  consumers.
- Do not mix this deletion with any semantic refactor or feature work.
- Do not promote this task to `tasks/active/` before the prerequisite gate
  exits 0.

## Context
- Owning subsystem/layer: `src/legacy/RHI/` removal; promoted owner
  `src/graphics/rhi/` (`Extrinsic.RHI.*`).
- Driver: legacy-retirement program seeded by
  [`LEGACY-002`](../../done/LEGACY-002-seed-src-legacy-retirement-backlog.md) under
  [`ARCH-004`](../../done/ARCH-004-legacy-retirement-first-deletion-target.md);
  structural template is
  [`LEGACY-001`](LEGACY-001-delete-src-legacy-interface.md).
- Subtree size: 54 files. Legacy module names (bare, non-`Extrinsic.`): the
  primary module `RHI` plus `RHI.*` (e.g. `RHI.Buffer`, `RHI.CommandContext`,
  `RHI.Bindless`, `RHI.Context`, `RHI.CudaDevice`).
- Promoted equivalent: `src/graphics/rhi/` (`Extrinsic.RHI.*`).
- CMake references: `INTRINSIC_LEGACY_RHI_SOURCE_ROOT` (`CMakeLists.txt` ~L231)
  and `add_subdirectory(${INTRINSIC_LEGACY_RHI_SOURCE_ROOT})` under
  `if(NOT INTRINSIC_HEADLESS_NO_GLFW)` (~L272).
- Layering allowlist (`tools/repo/layering_allowlist.yaml`) carries
  grandfathered rows keyed under `src/legacy/RHI/`; drop only those.
- Prerequisite update (2026-06-18): the consumer-grep gate FAILS — legacy
  `RHI`/`RHI.*` modules are still imported by `src/legacy/Graphics/`,
  `src/legacy/Asset/`, `src/legacy/Runtime/`, `src/legacy/Interface/`, and
  16 legacy compatibility tests after `LEGACY-037`. Promotion is blocked until
  those migrate to promoted APIs or retire with their owning legacy subtrees
  (typically alongside `LEGACY-008`, `LEGACY-010`, and `LEGACY-012`).
- `GRAPHICS-086` retired the semantic RHI/CUDA parity audit: legacy
  command helpers, persistent descriptors, swapchain/image ownership,
  scene-instance convenience APIs, and CUDA no longer represent unnamed
  blockers for this deletion. Future CUDA work must open a new opt-in
  method/backend task with a concrete workload.
- `LEGACY-035` retired the Vulkan deferred-destruction checks formerly embedded
  in the legacy runtime maintenance-lane test as legacy RHI implementation
  detail. A future promoted Vulkan deletion contract requires a fresh
  value-gated graphics task; it is no longer an unnamed blocker for this
  deletion.

## Required changes
- [ ] (Prerequisite, verified before promotion to `tasks/active/`) Run the
      consumer-grep gate in Verification and confirm it exits 0 with
      `OK: no external consumers ...`. Record the empty output in the commit
      message.
- [ ] Delete the `src/legacy/RHI/` subtree (54 files, including its
      `CMakeLists.txt`).
- [ ] Remove the `add_subdirectory(${INTRINSIC_LEGACY_RHI_SOURCE_ROOT})` line
      and the `INTRINSIC_LEGACY_RHI_SOURCE_ROOT` assignment from `CMakeLists.txt`.
- [ ] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key
      begins with `src/legacy/RHI/`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [ ] Strike legacy `RHI`/`RHI.*` mentions from
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
- [ ] `src/legacy/RHI/` no longer exists.
- [ ] `CMakeLists.txt` no longer references the removed subtree.
- [ ] Allowlist row count drops by the count of removed rows; no rows added.
- [ ] Default CPU gate is green.
- [ ] Commit is mechanical only.

## Verification
```bash
# Gate: fail loudly if any consumer of legacy RHI/RHI.* modules remains outside
# the doomed subtree. `git grep` exits 0 on match, so invert it.
# `import\s+RHI\b` matches `import RHI;` / `import RHI.Buffer;` but NOT the
# promoted `import Extrinsic.RHI.*;`.
if git grep -nE '^\s*(export\s+)?import\s+RHI\b' \
       -- 'src/**' 'tests/**' ':!src/legacy/RHI/**'; then
    echo "ERROR: src/legacy/RHI/ consumers remain outside the doomed subtree." >&2
    echo "       LEGACY-009 prerequisite NOT satisfied; do not promote to tasks/active/." >&2
    exit 1
fi
echo "OK: no external consumers of src/legacy/RHI/ remain."

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
- Adding new code under `src/graphics/rhi/` to "replace" the deleted files in
  this same commit.
- Granting new allowlist rows under cover of this deletion.
- Removing more than the `RHI/` subtree.
- Promoting this task to `tasks/active/` before the consumer-grep prerequisite
  is satisfied.

## Maturity
- Target: `Retired` (mechanical deletion of the legacy subtree).
- No `Operational` follow-up is owed by this deletion; promoted ownership
  lives in `src/graphics/rhi/` and `src/graphics/vulkan/` (see
  `docs/migration/nonlegacy-parity-matrix.md`). The remaining parity audit —
  command helpers, persistent descriptors, swapchain/image state,
  scene-instance convenience, and the CUDA decision — was retired by
  `GRAPHICS-086`; this deletion should now be promoted only after the
  consumer-grep gate is clean.
- The consumer-grep gate in Verification must exit 0 before this task is
  promoted to `tasks/active/`; this deletion is typically coordinated with
  `LEGACY-008` (legacy Graphics) because the two subtrees import each other.
