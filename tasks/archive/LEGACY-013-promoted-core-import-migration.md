---
id: LEGACY-013
theme: F
depends_on: [LEGACY-011]
---
# LEGACY-013 — Migrate promoted Core imports off legacy modules

## Goal
- [x] Move the remaining promoted, non-test source consumers of bare legacy
      `Core.*` modules to the promoted `Extrinsic.Core.*` modules so the
      promoted-src subset of the `LEGACY-005` Core deletion gate is clear.

## Non-goals
- Do not delete `src/legacy/Core/` or any other legacy subtree.
- Do not migrate legacy-internal consumers or broad compatibility-test suites;
  those stay with subtree deletion ordering and `LEGACY-012`. Direct tests of
  the promoted geometry APIs changed here may switch to promoted Core types to
  keep the public API and verification gate consistent.
- Do not add compatibility re-exports or local promoted modules named `Core.*`.
- Do not change geometry/runtime behavior beyond replacing the imported Core
  API owner.

## Context
- Owner/layer: promoted `src/geometry/` and one `src/runtime/` bridge file.
- `docs/migration/legacy-removal-audit.md` identifies the only legacy modules
  still imported by promoted, non-test engine code: legacy `Core.Memory`,
  `Core.Logging`, `Core.Error`, `Core.Handle`, and `Core.ResourcePool`.
- Promoted replacements already exist as `Extrinsic.Core.Memory`,
  `Extrinsic.Core.Logging`, `Extrinsic.Core.Error`,
  `Extrinsic.Core.StrongHandle`, and `Extrinsic.Core.ResourcePool`.
- `LEGACY-005` remains a pure mechanical deletion task and must not absorb this
  semantic migration.

## Required changes
- [x] Replace promoted `src/geometry/**` imports of bare `Core.*` modules with
      promoted `Extrinsic.Core.*` imports.
- [x] Replace `src/runtime/Runtime.AssetGeometryIO.cpp`'s remaining bare
      `Core.Error` import with promoted error types.
- [x] Remove the now-unneeded `IntrinsicCore` link edge from promoted
      `IntrinsicGeometry`.
- [x] Migrate directly affected geometry tests to the promoted `Extrinsic.Core`
      memory/error types used by the new geometry API signatures.
- [x] Update migration docs/task records to state that the promoted-src subset
      of the Core gate is clear while legacy-internal and test consumers remain.

## Tests
- [x] Build the affected promoted targets through `IntrinsicTests`.
- [x] Run the default CPU-supported CTest gate.
- [x] Run strict layering and test-layout checks.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/legacy-retirement.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` if the Core row's
      blocker text changes.
- [x] Update `LEGACY-005` and `LEGACY-012` task records if their consumer-grep
      blocker notes change.

## Acceptance criteria
- [x] `git grep` over promoted `src/**` excluding `src/legacy/**` reports no
      bare legacy module imports.
- [x] The broader `LEGACY-005` gate still records its remaining test and
      legacy-internal consumers instead of claiming Core is deletable.
- [x] No compatibility alias or re-export for legacy `Core.*` is introduced.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: this local commit.
- The promoted-src grep is clean. The broader `LEGACY-005` gate now reports
  133 legacy-internal consumers and 44 remaining test consumers; `LEGACY-012`
  owns those remaining test migrations, and legacy-internal consumers retire by
  Runtime-first to Core-last subtree ordering.

## Verification results
- `cmake --preset ci` — passed.
- `cmake --build --preset ci --target IntrinsicTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 3096/3096 tests.
- Promoted-src bare legacy import grep — clean.
- `python3 tools/repo/check_layering.py --root src --strict` — passed.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — passed.
- `tools/ci/run_clean_workshop_review.sh . --strict` — passed automated rows; manual scorecard rows remain reviewer-owned.
- `git diff --check` — passed.

## Verification
```bash
git grep -nE '^\s*(export\s+)?import\s+(Core|ECS|Graphics|RHI|Runtime|Asset|Interface)\b' \
    -- 'src/**' ':!src/legacy/**' | grep -vE 'import\s+Extrinsic'

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Geometry|AssetGeometryIO|GraphIO|MeshIO|PointCloudIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Deleting `src/legacy/Core/`.
- Editing legacy source to make the grep pass.
- Adding new Core API surface under this migration.
- Changing test labels or layout policy.

## Maturity
- Target: `CPUContracted` for the promoted-src import cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup needed
  before mechanical legacy subtree deletion can progress.
