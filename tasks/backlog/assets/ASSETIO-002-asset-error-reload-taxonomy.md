# ASSETIO-002 — Asset error and reload taxonomy

## Goal
- Define the asset error/load-state/reload/destroy-order contracts that improve deterministic promoted asset behavior without recreating the legacy asset manager.

## Non-goals
- No GPU upload implementation in `src/assets`; runtime and graphics remain responsible for upload handoff and residency.
- No file-format decoder work; conditional KTX support is owned by `ASSETIO-003`, and representative file-format coverage is owned by `ASSETIO-004`.
- No legacy `Asset.*` compatibility module or re-export.

## Context
- Owner/layer: `assets -> core`; runtime registers concrete geometry/model/texture callbacks and owns cross-layer wiring.
- `ASSETIO-001` promoted registry, payload store, path index, import routing, model/texture payloads, and runtime-to-graphics handoff. The parity matrix value-gates deterministic asset error/reload taxonomy, hot-reload atomicity, reload/destroy ordering, and ingest-state-machine follow-ups.
- Reuse `Asset.Service`, `Asset.EventBus`, `Asset.LoadPipeline`, `Asset.PayloadStore`, and `Runtime.AssetModelTextureHandoff` instead of adding a second asset manager.

## Value gate
- Current state: promoted assets already own CPU registry, payload storage, path lookup, import routing, event fanout, and model/texture payload handoff.
- Improvement: failed reloads, destroy ordering, and callback failures become deterministic and testable through CPU-only seams.
- Scope decision: retain only operation-status, reload-atomicity, and destroy-order rules. Do not copy legacy manager lifecycle, global asset state, or GPU residency behavior.

## Required changes
- [ ] Inventory legacy `Asset.Errors`, `Asset.Manager`, and `Asset.Pipeline` states against promoted `Core::ErrorCode`, `AssetService`, and `AssetLoadPipeline` diagnostics.
- [ ] Define a promoted asset operation status taxonomy for load, reload, destroy, callback failure, validation failure, and unsupported format cases.
- [ ] Add reload atomicity rules: failed reload preserves last good payload and generation; successful reload publishes deterministic generation/event ordering.
- [ ] Add destroy ordering rules: dependent payloads and ready events are drained deterministically without graphics ownership in `assets`.
- [ ] Wire runtime handoff tests that observe reload/destroy events without importing graphics into `assets`.

## Tests
- [ ] Add `unit;assets` tests for load-state transitions, failure diagnostics, reload atomicity, destroy ordering, and generation/event ordering.
- [ ] Add `integration;runtime` or `contract;runtime` coverage proving runtime subscribers see reload/destroy events in deterministic order.
- [ ] Add regression coverage for callback error propagation through `Asset.GeometryIOBridge` and `Asset.ModelTextureIOBridge`.

## Docs
- [ ] Update `src/assets/README.md` with the promoted taxonomy and reload/destroy contract.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` asset and runtime rows.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] Asset reload failure cannot publish a partially updated payload or stale successful event.
- [ ] Destroy and reload events are deterministic and test-covered through CPU-only seams.
- [ ] Legacy `Asset.Errors` behavior has a promoted replacement or an explicit retirement note.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'unit|contract|integration' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing geometry, runtime, graphics, RHI, platform, or app from `src/assets`.
- Encoding GPU residency decisions in asset payload ownership.

## Maturity
- Target: `CPUContracted` for asset taxonomy/reload behavior.
- No `Operational` follow-up is owed for CPU-only asset taxonomy/reload contracts.
