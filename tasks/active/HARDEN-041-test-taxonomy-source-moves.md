# HARDEN-041 — Move remaining test sources into taxonomy directories

## Goal
Move remaining wrapper test sources from legacy subsystem directories into the canonical taxonomy directories without changing test semantics.

## Non-goals
- Do not change test logic, assertions, fixtures, or runtime behavior.
- Do not alter production source under `src/`.
- Do not remove `src/legacy/` or perform legacy module migration.

## Context
HARDEN-040 completed the audit of non-taxonomic test directories and mapped concrete follow-up actions for source movement. This task performs only the mechanical source relocation and associated CMake list path rewires needed to keep builds/tests equivalent.

## Required changes
- Move wrapper test `*.cpp` files from:
  - `tests/Asset/`
  - `tests/Core/`
  - `tests/ECS/`
  - `tests/Graphics/`
  - `tests/Runtime/`
  into canonical taxonomy roots:
  - `tests/unit/`
  - `tests/contract/`
  - `tests/integration/`
  according to the mapping recorded in `docs/reports/test-taxonomy-audit-2026-04-29.md`.
- Update affected `tests/CMakeLists.txt` (and subordinate CMake files if present) to point to relocated files.
- Keep target names and labels stable unless a taxonomy-only rename is explicitly required by the audit mapping.
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` status/evidence for HARDEN-041.

## Tests
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Update hardening tracker status board and evidence log for HARDEN-041.
- Keep links to the HARDEN-040 audit report current.

## Acceptance criteria
- [ ] Wrapper source files are relocated from old subsystem directories into taxonomy directories per HARDEN-040 mapping.
- [ ] CMake test registration resolves relocated files without missing-source or missing-target errors.
- [ ] CPU-supported CTest gate remains green after the move.
- [ ] Hardening tracker records HARDEN-041 progress and verification evidence.
- [ ] Strict task/doc validators pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No test semantic refactors in the same patch as mechanical moves.
- No changes to production runtime/graphics behavior.
- No legacy retirement or `src/legacy/` shrink work.
