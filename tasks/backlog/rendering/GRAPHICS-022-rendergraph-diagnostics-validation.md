# GRAPHICS-022 — Rendergraph diagnostics and validation

## Goal
- Implement and verify a deterministic, testable diagnostics surface for rendergraph validation so invalid frame-graph state fails early without Vulkan dependencies.

## Non-goals
- No new rendering features or pass shading behavior.
- No Vulkan-only validation paths that block CPU/null test coverage.
- No runtime/ECS ownership expansion into promoted graphics layers.
- No framegraph scheduler redesign beyond diagnostics requirements.

## Context
- Owner: `src/graphics/framegraph/` and `src/graphics/renderer/` validation surfaces consumed by runtime wiring.
- This task fills a missing task home identified by `GRAPHICS-021` and should be treated as rendering infrastructure hardening, not feature delivery.
- Diagnostics must remain deterministic and unit-testable to support CI without GPU availability.
- Architecture contract reminders: promoted graphics consumes snapshots/views and must not depend on live ECS ownership.

## Required changes
- Add rendergraph introspection coverage for:
  - pass submission order;
  - resource producers and consumers;
  - attachment metadata;
  - imported versus transient resource status;
  - first and last read/write pass indices.
- Add a structured validation result object that reports:
  - severity (`error`/`warning`);
  - missing-producer errors;
  - transient resource without producer errors;
  - `LOAD` without guaranteed earlier writer warning/error according to policy;
  - unauthorized imported-resource write diagnostics.
- Enforce imported backbuffer write policy in validation:
  - imported `Backbuffer` may only be written by present/finalization stages.
- Ensure debug dump output used by tests is stable and deterministic.

## Tests
- Add `contract;graphics` tests for validation diagnostics and policy failures.
- Add `unit;graphics` tests for deterministic debug dump formatting where applicable.
- Ensure tests are runnable without Vulkan.

## Docs
- Update `docs/architecture/rendering-three-pass.md` validation/audit section with the diagnostics contract.
- Update `src/graphics/renderer/README.md` if public rendergraph diagnostics APIs or ownership notes change.
- Cross-link this task from `GRAPHICS-001`/rendering backlog index material when adding implementation references.

## Acceptance criteria
- Rendergraph diagnostics are testable in CPU/null environments without Vulkan.
- Invalid graph states fail deterministically with structured diagnostics.
- Imported backbuffer write policy is enforced by validation or explicitly surfaced in diagnostics.
- Debug dump formatting used by tests is deterministic.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No renderer feature implementation.
- No shader changes.
- No Vulkan-only mandatory test coverage.
- No unrelated backlog rewrites outside task cross-link updates.
