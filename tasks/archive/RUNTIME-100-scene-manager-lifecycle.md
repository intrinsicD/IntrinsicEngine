# RUNTIME-100 — Scene manager lifecycle and persistence boundary

## Status
- Status: done.
- Completed: 2026-06-09.
- Owner/agent: Codex.
- Branch: `main`.
- Final implementation commit: this retirement commit.
- Maturity: `CPUContracted` for backend-neutral scene lifecycle and persistence diagnostics.
- Summary: Runtime scene replacement now uses one boundary for load/new/close: scene loads deserialize into a temporary registry before replacement, render-extraction scene sidecars and per-entity bindings are drained, selection hover/pending/in-flight pick state is cleared, refined primitive state resets, and stable lookup rebuilds or clears. Scene serialization now reports unsupported persistence families in stats, and docs classify supported/deferred/retired component families.

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
- [x] Define runtime scene lifecycle operations: create/clear/replace scene, pre-clear sidecar drain, post-load stable lookup rebuild, selection/refinement cleanup, physics-world reset, and render-extraction cache retire.
- [x] Classify persistence scope for components not covered by `RUNTIME-098`, including lights, shadows, collider/rigid-body descriptors, spatial-debug bindings, visualization config/bindings, asset instances, and camera/editor state.
- [x] Add explicit policy for arbitrary legacy asset-source reimport: supported through promoted asset IDs/path index, deferred, or retired.
- [x] Add deterministic diagnostics for unsupported component persistence rather than silently dropping state.
- [x] Keep ECS as CPU data authority and runtime as the lookup/sidecar owner.

## Tests
- [x] Add `contract;runtime` scene reset tests proving sidecars, selection, physics, visualization adapters, and extraction caches are cleaned up deterministically.
- [x] Add persistence round-trip tests for any newly supported components. No newly supported component family was added beyond existing `RUNTIME-098` support; existing round-trip coverage remains the support proof.
- [x] Add fail-closed diagnostics tests for intentionally unsupported components.

## Docs
- [x] Update `src/runtime/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Update scene serialization docs or add a runtime scene lifecycle section.
- [x] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [x] Runtime owns a single tested scene replacement lifecycle that all editor/app paths use.
- [x] Full-component persistence has explicit supported/deferred/retired status by component family.
- [x] Legacy scene-manager deletion blockers are concrete consumer-grep gates or named follow-ups.

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Scene|Serialization|Stable|Selection|Physics|Extraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
git diff --check
```

Result: configure succeeded; `IntrinsicTests` built; focused CTest passed
355/355; full CPU-supported CTest passed 2878/2878. Structural, docs, task-link,
inventory, and whitespace checks passed.

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
