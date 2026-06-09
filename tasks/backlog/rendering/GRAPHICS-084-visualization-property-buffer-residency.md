---
id: GRAPHICS-084
theme: F
depends_on: []
---
# GRAPHICS-084 — Visualization property-buffer residency

## Goal
- Add GPU residency for selected visualization property arrays needed by current promoted scalar/color/vector/isoline workflows, without recreating legacy graphics property upload helpers.

## Non-goals
- No geometry algorithm ownership in graphics.
- No live ECS, runtime, or asset-service reads from graphics.
- No UI property enumeration; `UI-005` already owns the editor command surface.
- No persistent overlay entity lifecycle; `RUNTIME-104` owns producers.

## Context
- Owner/layer: `graphics` consumes immutable packet data and owns GPU upload/residency; runtime owns property selection and supplies copied metadata/snapshot inputs.
- `RUNTIME-083` produces visualization adapter packets that currently rely on externally supplied buffer-device addresses. `UI-005` explicitly deferred graphics-owned residency for property arrays until current visualization workflows justify selected domains and types.
- Reuse `Graphics.VisualizationOverlayUploadHelper`, `Graphics.GpuWorld` managed buffers, `Graphics.GpuAssetCache` retire patterns, frame-anchored deferred destruction, and existing visualization packet validation.

## Value gate
- Current state: UI and runtime can select visualization properties, but current packets rely on externally supplied GPU addresses for some property-array workflows.
- Improvement: graphics owns residency for selected property arrays, so runtime/UI stay data-only and no legacy upload helper is needed.
- Scope decision: retain only property domains/types used by current promoted adapters or checked-in tests. Do not build an arbitrary property-buffer system without a consumer.

## Required changes
- [ ] Inventory current promoted visualization adapters and tests to select the retained property domains/types.
- [ ] Define a backend-neutral property-buffer upload descriptor covering element count, stride/type, domain, dirty stamp, and diagnostic source key.
- [ ] Add retained or per-frame upload policy for scalar, color, vector, and label arrays, reusing existing upload/retire helpers where possible.
- [ ] Publish stable buffer addresses or handles into visualization packets without runtime owning GPU resources.
- [ ] Add fail-closed diagnostics for unsupported type, zero elements, non-finite values, stale dirty stamps, and upload deferral.
- [ ] Keep upload validation centralized and avoid duplicating adapter-specific logic in backend code.

## Tests
- [ ] Add `contract;graphics` CPU/null tests for upload descriptor validation, dirty-stamp reuse, retire ordering, and packet address publication.
- [ ] Add `integration;runtime;graphics` tests proving runtime adapter packets can bind graphics-owned property buffers.
- [ ] Add opt-in `gpu;vulkan` visualization smoke if buffer-address use requires backend proof.

## Docs
- [ ] Update `src/graphics/renderer/README.md`, `docs/architecture/graphics.md`, and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update `tasks/backlog/rendering/README.md`.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Selected promoted property arrays can become graphics-owned visualization buffers through a tested residency seam.
- [ ] Runtime/UI no longer need to supply external GPU buffer addresses for current property-set visualization workflows.
- [ ] Diagnostics distinguish invalid source data from backend upload deferral.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Visualization|Property|GpuWorld|Upload' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime/ECS/asset services into graphics.
- Adding a second visualization packet validation system.

## Maturity
- Target: `CPUContracted`; `Operational` GPU/Vulkan proof is required before claiming visual backend parity.
