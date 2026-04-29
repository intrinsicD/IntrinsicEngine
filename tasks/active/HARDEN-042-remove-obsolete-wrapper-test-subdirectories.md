# HARDEN-042 — Remove or formalize old subsystem test subdirectories

## Goal
Remove obsolete wrapper test subdirectory build stubs after HARDEN-041 source relocation, while preserving shared test support includes through taxonomy-owned paths.

## Non-goals
- Do not change test logic, fixtures, or assertions.
- Do not modify production source under `src/`.
- Do not perform legacy retirement or `src/legacy/` shrink work.

## Context
HARDEN-041 mechanically relocated all wrapper `*.cpp` sources from `tests/{Asset,Core,ECS,Graphics,Runtime}` into taxonomy directories. The remaining wrapper directories still contained stale `CMakeLists.txt` files pointing to removed `Extrinsic*` test targets, plus one shared helper header in `tests/Graphics/MockRHI.hpp`.

## Required changes
- Remove obsolete wrapper `CMakeLists.txt` files under:
  - `tests/Asset/`
  - `tests/Core/`
  - `tests/ECS/`
  - `tests/Graphics/`
  - `tests/Runtime/`
- Relocate `tests/Graphics/MockRHI.hpp` into `tests/support/MockRHI.hpp`.
- Keep the patch mechanical only and avoid semantic test behavior changes.
- Update the hardening tracker (`tasks/active/0001-post-reorganization-hardening-tracker.md`) status/evidence for HARDEN-042.

## Tests
- `rg -n "ExtrinsicAssetTests|ExtrinsicCoreTests|ExtrinsicECSTests|ExtrinsicGraphicsTests|ExtrinsicRuntimeTests" tests`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md`:
  - status board row for HARDEN-042.
  - temporary wrapper subdirectory note under temporary test skips/quarantines.
  - evidence log entries for HARDEN-042 execution.

## Acceptance criteria
- [x] No wrapper `CMakeLists.txt` remain under `tests/Asset`, `tests/Core`, `tests/ECS`, `tests/Graphics`, `tests/Runtime`.
- [x] Taxonomy tests still resolve `MockRHI.hpp` from shared test support includes.
- [x] Strict task policy validator passes.
- [x] Strict docs link validator passes.

## Verification
```bash
rg -n "ExtrinsicAssetTests|ExtrinsicCoreTests|ExtrinsicECSTests|ExtrinsicGraphicsTests|ExtrinsicRuntimeTests" tests
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No reintroduction of wrapper-target CMake registration for removed `Extrinsic*` suites.
- No test semantic refactors mixed with wrapper-subdirectory cleanup.
- No production runtime/graphics behavior changes.

## Execution log
- 2026-04-29: Removed obsolete wrapper `CMakeLists.txt` files and relocated `MockRHI.hpp` to `tests/support/MockRHI.hpp`.
- 2026-04-29: Ran strict task/docs validators and stale wrapper-target grep check.
