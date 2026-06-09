# RUNTIME-100 — Scene manager lifecycle and persistence boundary

## Goal
- Promote only the useful scene-manager lifecycle behavior as runtime-owned world reset, entity lifecycle, stable-identity rebuild, sidecar cleanup, and explicit persistence boundaries.

## Non-goals
- No live GPU handles or graphics-owned resources in ECS components.
- No renderer/RHI cache serialization.
- No file-dialog UI; `UI-008` owns editor workflow affordances.
- No legacy `Runtime.SceneManager` or `Runtime.SceneSerializer` compatibility imports.
- No blanket full-component persistence merely because legacy stored a component family.

## Context
- Owner/layer: `runtime` owns scene composition and can coordinate ECS, assets, physics, and graphics extraction sidecars.
- `RUNTIME-098` promoted backend-neutral JSON scene save/load for current sandbox-authored ECS data. The parity matrix value-gates scene-manager lifecycle, supported/deferred/retired persistence decisions, arbitrary legacy asset-source reimport, and final legacy deletion as later decisions.
- Reuse `Runtime.SceneSerialization`, `Runtime.StableEntityLookup`, `Runtime.RenderExtraction` shutdown/retire APIs, `Runtime.PhysicsBridge`, and `AssetService`.

## Value gate
- Current state: promoted scene serialization already covers current sandbox-authored geometry, transforms, hierarchy, stable IDs, and selection eligibility.
- Improvement: a single scene replacement lifecycle prevents stale runtime sidecars, physics bodies, selection caches, and extraction resources after load/reset.
- Scope decision: retain lifecycle/reset/cache cleanup. For persistence, classify each component family as supported, deferred, or retired; do not chase full legacy serialization by default.

## Required changes
- [ ] Define runtime scene lifecycle operations: create/clear/replace scene, pre-clear sidecar drain, post-load stable lookup rebuild, selection/refinement cleanup, physics-world reset, and render-extraction cache retire.
- [ ] Classify persistence scope for components not covered by `RUNTIME-098`, including lights, shadows, collider/rigid-body descriptors, spatial-debug bindings, visualization config/bindings, asset instances, and camera/editor state.
- [ ] Add explicit policy for arbitrary legacy asset-source reimport: supported through promoted asset IDs/path index, deferred, or retired.
- [ ] Add deterministic diagnostics for unsupported component persistence rather than silently dropping state.
- [ ] Keep ECS as CPU data authority and runtime as the lookup/sidecar owner.

## Tests
- [ ] Add `contract;runtime` scene reset tests proving sidecars, selection, physics, visualization adapters, and extraction caches are cleaned up deterministically.
- [ ] Add persistence round-trip tests for any newly supported components.
- [ ] Add fail-closed diagnostics tests for intentionally unsupported components.

## Docs
- [ ] Update `src/runtime/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update scene serialization docs or add a runtime scene lifecycle section.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Runtime owns a single tested scene replacement lifecycle that all editor/app paths use.
- [ ] Full-component persistence has explicit supported/deferred/retired status by component family.
- [ ] Legacy scene-manager deletion blockers are concrete consumer-grep gates or named follow-ups.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Scene|Serialization|Stable|Selection|Physics|Extraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Serializing renderer/RHI implementation details.
- Storing live physics, graphics, or runtime sidecar handles in ECS.

## Maturity
- Target: `CPUContracted` for scene lifecycle and persistence contracts.
- No `Operational` follow-up is owed for backend-neutral scene lifecycle contracts.
