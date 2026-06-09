# ASSETIO-004 — Representative file-format visual coverage

## Status
- Status: done.
- Completed: 2026-06-09.
- Owner/agent: Codex.
- Branch: `main`.
- Final implementation commit: this retirement commit.
- Maturity: `CPUContracted`.
- Summary: Added a small generated fixture matrix for promoted runtime import
  coverage: OBJ mesh, TGF graph, ASCII PLY point cloud, GLTF model-scene with an
  external binary buffer plus embedded PNG material texture, and standalone PNG
  texture import. Model-scene material records now defer texture binding when
  child texture assets are not GPU-ready, re-resolve after texture upload/reload,
  and distinguish upload deferral from hard materialization or decode failures.
  Headless/null-device texture uploads now remain retryable deferrals instead of
  marking the GPU cache entry failed or failing CPU asset import.

## Goal
- Prove promoted asset/geometry/runtime import paths across a small representative fixture matrix that improves confidence in current workflows, including post-upload material re-resolution where model-scene assets actually need it.

## Non-goals
- No new decoder implementation unless a coverage gap is discovered and filed as a separate task.
- No legacy importer/exporter imports from tests or promoted code.
- No deletion of legacy IO modules; deletion remains under `LEGACY-004`, `LEGACY-008`, and `LEGACY-010`.

## Context
- Owner/layer: cross-domain verification task rooted in `assets` and `runtime`, with graphics proof only through existing renderer/GPU seams.
- `GEOIO-002`, `ASSETIO-001`, `UI-007`, and `RUNTIME-095` cover the promoted import route and one scoped operational sandbox proof. This slice closes the unproven representative CPU/null import matrix and material texture re-resolution gap.
- Fixtures are generated inline by `tests/contract/runtime/Test.AssetImportFormatCoverage.cpp`; no large external datasets were added to the default gate.

## Value gate
- Current state: promoted import/materialization worked for the scoped sandbox path and many geometry IO codecs already had CPU coverage.
- Improvement: the representative matrix catches integration regressions without making legacy's broad IO surface the new default.
- Scope decision: retained only currently supported formats used by near-term workflows: OBJ, TGF, PLY point cloud, GLTF/GLB model-scene coverage through GLTF, and PNG texture coverage. Unsupported or rarely used legacy formats remain deferred or retired by their own tasks, such as `ASSETIO-003` for KTX/KTX2.

## Required changes
- [x] Define a small fixture matrix from current supported formats and workflow needs; do not include a format only because legacy handled it.
- [x] Add runtime import smoke coverage proving decoded payloads materialize renderable/selectable `GeometrySources` or texture assets as appropriate.
- [x] Add material re-resolution after texture upload/reload so model-scene material bindings observe child texture asset readiness.
- [x] Add diagnostics that distinguish decode failure, materialization failure, upload deferral, and visual-readback failure. This slice adds upload-deferral diagnostics; no new visual-readback failure path was introduced because GPU readback stays opt-in and covered by existing sandbox smoke ownership.
- [x] Keep broad GPU/Vulkan readback proof opt-in and skip-safe on hosts without an operational Vulkan lane. Existing `RUNTIME-095` remains the operational sandbox proof; ASSETIO-004 does not add a second default-gate GPU requirement.

## Tests
- [x] Add CPU/null import materialization tests for each fixture domain.
- [x] Add `contract;runtime` material-binding re-resolution tests after texture Ready/reload events.
- [x] Keep opt-in `gpu;vulkan` visual proof out of the default CPU gate. RUNTIME-095 owns the scoped operational sandbox proof; a broader file-backed GPU readback matrix requires a future `Operational` task if product value justifies it.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the coverage matrix and any deferred formats.
- [x] Update `tasks/backlog/assets/README.md` and `tasks/backlog/README.md`.
- [x] Document fixture provenance and size policy near the tests or in `tests/README.md` if labels change. No label change was made; fixture provenance is inline in the generated fixture test.

## Acceptance criteria
- [x] Every format in the selected fixture matrix has a deterministic promoted import result and test evidence.
- [x] Material texture bindings can re-resolve after child texture upload/reload without rerunning legacy import code.
- [x] GPU/Vulkan visual proof is opt-in, labelled, and not part of the default CPU gate. Existing `RUNTIME-095` GPU/Vulkan coverage remains opt-in; ASSETIO-004 retires at `CPUContracted`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetImportFormatCoverage|RuntimeAssetModelSceneHandoff' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicGraphicsAssetsUnitTests
ctest --test-dir build/ci --output-on-failure -R 'GpuAssetCache' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'Asset|Import|Sandbox|Model|Texture' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
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

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding large third-party datasets to the default test gate.
- Importing legacy IO modules in new tests.

## Maturity
- Completed: `CPUContracted` for the representative fixture matrix and material re-resolution contract.
- `Operational`: remains owned by opt-in GPU/Vulkan sandbox coverage such as `RUNTIME-095` or a future value-gated file-backed readback task.
