# LEGACY-010 — Delete `src/legacy/Runtime/`

## Goal
- [ ] Remove the `src/legacy/Runtime/` subtree (29 files) as a single mechanical
      commit, retire its `add_subdirectory(${INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT})`
      entry and the `INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT` assignment from the
      top-level CMake graph, drop the layering-allowlist rows granted only for
      this subtree, and regenerate the module inventory.

## Non-goals
- Do not delete any other `src/legacy/` subtree under cover of this task. The
  `Runtime.EditorUI` module was retired separately by
  [`LEGACY-007`](../../done/LEGACY-007-delete-src-legacy-editorui.md).
- Do not introduce new promoted code paths in this commit; the promoted
  `src/runtime/` (`Extrinsic.Runtime.*`) surface must already carry the
  consumers.
- Do not mix this deletion with any semantic refactor or feature work.
- Do not promote this task to `tasks/active/` before the prerequisite gate
  exits 0.

## Context
- Owning subsystem/layer: `src/legacy/Runtime/` removal; promoted owner
  `src/runtime/` (`Extrinsic.Runtime.*`).
- Driver: legacy-retirement program seeded by
  [`LEGACY-002`](LEGACY-002-seed-src-legacy-retirement-backlog.md) under
  [`ARCH-004`](../../done/ARCH-004-legacy-retirement-first-deletion-target.md);
  structural template is
  [`LEGACY-001`](LEGACY-001-delete-src-legacy-interface.md).
- Subtree size: 29 files. Legacy module names (bare, non-`Extrinsic.`):
  `Runtime.Engine`, `Runtime.FrameLoop`, `Runtime.GraphicsBackend`,
  `Runtime.PointCloudKMeans`, `Runtime.RenderExtraction`,
  `Runtime.RenderOrchestrator`, `Runtime.ResourceMaintenance`,
  `Runtime.AssetIngestService`, `Runtime.SceneManager`,
  `Runtime.SceneSerializer`, `Runtime.Selection`, `Runtime.SelectionModule`,
  `Runtime.SystemBundles`. The former `Runtime.EditorUI` module was owned by
  the separate EditorUI subtree and is retired under `LEGACY-007`.
- Promoted equivalent: `src/runtime/` (`Extrinsic.Runtime.*`), the
  `RUNTIME-070..095` composition/extraction/selection promotion chain.
- CMake references: `INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT` (`CMakeLists.txt`
  ~L236) and `add_subdirectory(${INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT})` under
  `if(NOT INTRINSIC_HEADLESS_NO_GLFW)` (~L277; `# needs Asset`).
- Layering allowlist (`tools/repo/layering_allowlist.yaml`) carries
  grandfathered rows keyed under `src/legacy/Runtime/`; drop only those.
- Prerequisite (today, 2026-06-07): the consumer-grep gate FAILS — legacy
  `Runtime.*` modules are still imported by integration tests and remaining
  legacy subtrees. The legacy Sandbox consumer retired under `LEGACY-003`, and
  the legacy EditorUI consumer retired under `LEGACY-007`; remaining consumers
  must migrate to `Extrinsic.Runtime.*`.

## Required changes
- [ ] (Prerequisite, verified before promotion to `tasks/active/`) Run the
      consumer-grep gate in Verification and confirm it exits 0 with
      `OK: no external consumers ...`. Record the empty output in the commit
      message.
- [ ] Delete the `src/legacy/Runtime/` subtree (29 files, including its
      `CMakeLists.txt`).
- [ ] Remove the `add_subdirectory(${INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT})`
      line and the `INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT` assignment from
      `CMakeLists.txt`.
- [ ] Drop the allowlist rows in `tools/repo/layering_allowlist.yaml` whose key
      begins with `src/legacy/Runtime/`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [ ] Strike legacy `Runtime.*` mentions from
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
- [ ] `src/legacy/Runtime/` no longer exists.
- [ ] `CMakeLists.txt` no longer references the removed subtree.
- [ ] Allowlist row count drops by the count of removed rows; no rows added.
- [ ] Default CPU gate is green.
- [ ] Commit is mechanical only.

## Verification
```bash
# Gate: fail loudly if any consumer of legacy Runtime.* modules (excluding the
# EditorUI-owned Runtime.EditorUI) remains outside the doomed subtree.
# `git grep` exits 0 on match, so invert it.
if git grep -nE '^\s*(export\s+)?import\s+Runtime\.(Engine|FrameLoop|GraphicsBackend|PointCloudKMeans|RenderExtraction|RenderOrchestrator|ResourceMaintenance|AssetIngestService|SceneManager|SceneSerializer|Selection|SelectionModule|SystemBundles)\b' \
       -- 'src/**' 'tests/**' ':!src/legacy/Runtime/**'; then
    echo "ERROR: src/legacy/Runtime/ consumers remain outside the doomed subtree." >&2
    echo "       LEGACY-010 prerequisite NOT satisfied; do not promote to tasks/active/." >&2
    exit 1
fi
echo "OK: no external consumers of src/legacy/Runtime/ remain."

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
- Adding new code under `src/runtime/` to "replace" the deleted files in this
  same commit.
- Granting new allowlist rows under cover of this deletion.
- Removing more than the `Runtime/` subtree.
- Promoting this task to `tasks/active/` before the consumer-grep prerequisite
  is satisfied.
