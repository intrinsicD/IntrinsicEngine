# GRAPHICS-022 — Rendergraph diagnostics and validation

## Goal
Define and implement a deterministic, CPU-testable rendergraph diagnostics surface that validates pass/resource correctness before Vulkan execution.

## Non-goals
- No new rendering features, shading models, or pass visuals.
- No Vulkan-only validation path as the sole correctness mechanism.
- No replacement of existing framegraph ownership boundaries between runtime and graphics.

## Context
The rendering architecture expects explicit graph validation and inspectable diagnostics, but there is no single backlog task that owns this scope. A dedicated task is needed so agents can implement validation behavior (missing producers, illegal writes, load/store correctness) with deterministic outputs that run in the default CPU gate.

## Required changes
- Add rendergraph introspection coverage for:
  - pass order,
  - resource producers/consumers,
  - attachment metadata,
  - imported vs transient resource status,
  - first/last read and write pass indices.
- Define a structured validation result object with severity and stable diagnostics for:
  - missing producer errors,
  - transient resource without producer errors,
  - `LOAD` without guaranteed earlier writer warning/error according to policy,
  - unauthorized imported resource writes.
- Enforce or explicitly diagnose imported backbuffer write policy:
  - only present/finalization may write imported `Backbuffer`.
- Ensure debug dump/diagnostic output has deterministic formatting suitable for tests.

## Tests
- Add `contract;graphics` tests for rendergraph validation diagnostics behavior.
- Add `unit;graphics` tests for deterministic debug dump formatting where applicable.
- Keep tests CPU-capable; Vulkan/GPU execution must remain optional, not required.

## Docs
- Update `docs/architecture/rendering-three-pass.md` validation/audit section to reflect diagnostics ownership and behavior.
- Update `src/graphics/renderer/README.md` if public diagnostics APIs or contracts change.

## Acceptance criteria
- Rendergraph diagnostics are executable and testable in the default CPU gate.
- Invalid graphs fail deterministically with structured diagnostics.
- Imported backbuffer write policy is enforced or represented explicitly in diagnostics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No renderer feature implementation unrelated to diagnostics/validation.
- No Vulkan-only mandatory tests.
- No shader/pass behavior changes for visual output.
