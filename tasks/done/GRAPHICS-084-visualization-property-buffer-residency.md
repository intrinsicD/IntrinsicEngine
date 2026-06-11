---
id: GRAPHICS-084
theme: F
depends_on: [RUNTIME-083]
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
- Current promoted packet surface (`Graphics.VisualizationPackets`): scalar-attribute, color-attribute, and vector-field overlay packets validated by `ValidateVisualizationPackets()`. The consuming workflows are the `UI-005` visualization presets (scalar, isoline, color-buffer), which bounds the property domains/types the inventory step should retain.

## Value gate
- Current state: UI and runtime can select visualization properties, but current packets rely on externally supplied GPU addresses for some property-array workflows.
- Improvement: graphics owns residency for selected property arrays, so runtime/UI stay data-only and no legacy upload helper is needed.
- Scope decision: retain only property domains/types used by current promoted adapters or checked-in tests. Do not build an arbitrary property-buffer system without a consumer.

## Required changes
- [x] Inventory current promoted visualization adapters and tests to select the retained property domains/types: scalar float/double inputs are uploaded as scalar float buffers, color labels use RGBA float buffers, and vector-field properties use float3 buffers for current promoted adapters.
- [x] Define a backend-neutral property-buffer upload descriptor covering element count, stride/type, domain, dirty stamp, and diagnostic source key.
- [x] Add retained/per-frame upload policy for selected scalar, color, and vector arrays through renderer-owned `RHI::BufferManager` leases.
- [x] Publish stable buffer addresses into visualization packets without runtime owning GPU resources.
- [x] Add fail-closed diagnostics for unsupported type, zero elements, non-finite values, stale dirty stamps, and upload deferral.
- [x] Keep upload validation centralized in `Graphics.VisualizationPackets` and avoid duplicating adapter-specific logic in backend code.

## Tests
- [x] Add `contract;graphics` CPU/null tests for upload descriptor validation, dirty-stamp reuse, retire ordering, and packet address publication.
- [x] Add `integration;runtime;graphics` tests proving runtime adapter packets can bind graphics-owned property buffers.
- [x] Open `GRAPHICS-084C` for the opt-in `gpu;vulkan` visualization smoke; this task retires at `CPUContracted` and does not claim backend operational parity.

## Docs
- [x] Update `src/graphics/renderer/README.md`, `docs/architecture/graphics.md`, and `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Update `tasks/backlog/rendering/README.md`.
- [x] Regenerate module inventory because public module surfaces changed.

## Acceptance criteria
- [x] Selected promoted property arrays can become graphics-owned visualization buffers through a tested residency seam.
- [x] Runtime/UI no longer need to supply external GPU buffer addresses for current property-set visualization workflows.
- [x] Diagnostics distinguish invalid source data from backend upload deferral.

## Status
- Completed 2026-06-11 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Scope: runtime adapters emit copied CPU property-array descriptors when external BDAs are absent; graphics owns residency, validates descriptors, publishes BDAs into scalar/color/vector/isoline packets, and reports property-buffer diagnostics without importing runtime/ECS.
- Operational note: `GRAPHICS-084C` retired the opt-in Vulkan smoke; this retirement did not claim a fresh `gpu;vulkan` host run.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Visualization|Property|GpuWorld|Upload' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
bash -lc "set -o pipefail; ctest --test-dir build/ci --output-on-failure -R 'Visualization|Property|GpuWorld|Upload|RuntimeRenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 | tee /tmp/intrinsic_graphics084_ctest_final.log"
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
tools/ci/run_clean_workshop_review.sh . --strict
git diff --check
```

Result: configure succeeded with Clang 23; `IntrinsicTests` built; focused
CTest passed 195/195, including `VisualizationPropertyBufferResidencyContract.*`,
`VisualizationAdapters.*PropertyBufferWhenBdaMissing`, and
`RuntimeRenderExtraction.VisualizationScalarAdapterMissingBdaUploadsPropertyBuffer`
plus the stable-id-scoped source-key collision regression.
Layering, test layout, doc links, task policy, task-state links, session-brief
freshness, docs-sync diff checks, clean-workshop automated scorecard rows, and
`git diff --check` passed. The `gpu;vulkan` smoke was not run in this slice;
`GRAPHICS-084C` retired that operational proof. Root hygiene remains warning-mode with pre-existing
`.agents/` and `imgui.ini` root entries.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime/ECS/asset services into graphics.
- Adding a second visualization packet validation system.

## Maturity
- Target: `CPUContracted` for this retirement; `Operational` retired by `GRAPHICS-084C`.
- Slices A–B closed the backend-neutral contract. Slice C is descoped to `GRAPHICS-084C`, so visual backend parity is not claimed here.

## Slice plan
- **Slice A — descriptor and validation contract.** Define the backend-neutral upload descriptor (element count, stride/type, domain, dirty stamp, diagnostic source key) plus fail-closed validation diagnostics, with CPU/null `contract;graphics` tests. Defers upload policy and packet publication to Slice B.
- **Slice B — residency and packet publication.** Retained/per-frame upload policy through `Graphics.GpuWorld` managed buffers reusing `Graphics.VisualizationOverlayUploadHelper` patterns; publish stable buffer addresses/handles into visualization packets; `integration;runtime;graphics` binding tests. Preserves the default CPU gate.
- **Slice C — backend proof.** Opt-in `gpu;vulkan` visualization smoke proving graphics-owned buffer-address consumption on a Vulkan-capable host.
