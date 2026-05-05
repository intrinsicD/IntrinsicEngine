# Task 10 — Add missing task: rendergraph diagnostics and validation

- Status: completed (2026-05-02)
- Owner: Codex (current branch)
- Branch / PR: current branch / TBD
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: implementation remains in `tasks/active/GRAPHICS-022-rendergraph-diagnostics-validation.md`.
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict`.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Create a dedicated rendering task for rendergraph diagnostics and validation.

Create:

`tasks/active/GRAPHICS-022-rendergraph-diagnostics-validation.md`

## Goal

Implement and test the rendergraph diagnostics surface required by the canonical rendering architecture.

## Required scope

- Rendergraph introspection for:
  - pass order
  - resource producers/consumers
  - attachment metadata
  - imported/transient resource status
  - first/last read and write pass indices
- Validation result object with structured diagnostics:
  - error/warning severity
  - missing producer errors
  - transient resource without producer errors
  - LOAD without guaranteed earlier writer warning/error according to policy
  - unauthorized imported resource writes
- Imported backbuffer write policy:
  - only present/finalization may write imported `Backbuffer`.
- Debug dump stability for tests.

## Tests

- Add `contract;graphics` tests for validation diagnostics.
- Add `unit;graphics` tests for deterministic debug dump formatting if appropriate.
- Do not require Vulkan.

## Docs

- Update `docs/architecture/rendering-three-pass.md` validation/audit section.
- Update `src/graphics/renderer/README.md` if public APIs change.

## Acceptance criteria

- Rendergraph diagnostics are testable without Vulkan.
- Invalid graphs fail deterministically.
- The imported backbuffer write policy is enforced or explicitly represented in diagnostics.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- No rendering feature implementation.
- No Vulkan-only mandatory tests.
- No pass shader changes.

This extracts a real missing owner from the architecture doc's validation/audit expectations.
