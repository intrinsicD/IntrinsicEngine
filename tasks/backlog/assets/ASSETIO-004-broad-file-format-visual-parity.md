# ASSETIO-004 — Representative file-format visual coverage

## Goal
- Prove promoted asset/geometry/runtime import paths across a small representative fixture matrix that improves confidence in current workflows, including post-upload material re-resolution where model-scene assets actually need it.

## Non-goals
- No new decoder implementation unless a coverage gap is discovered and filed as a separate task.
- No legacy importer/exporter imports from tests or promoted code.
- No deletion of legacy IO modules; deletion remains under `LEGACY-004`, `LEGACY-008`, and `LEGACY-010`.

## Context
- Owner/layer: cross-domain verification task rooted in `assets` and `runtime`, with graphics proof only through existing renderer/GPU seams.
- `GEOIO-002`, `ASSETIO-001`, `UI-007`, and `RUNTIME-095` cover the promoted import route and one scoped operational sandbox proof. The parity matrix still calls out broad visual coverage and post-upload material re-resolution as unproven.
- Reuse checked-in fixtures under `assets/` or small generated fixtures; do not add large external datasets to the default gate.

## Value gate
- Current state: promoted import/materialization works for the scoped sandbox path and many geometry IO codecs already have CPU coverage.
- Improvement: a representative matrix catches integration regressions without making legacy's broad IO surface the new default.
- Scope decision: include only formats that are already supported, used by checked-in assets/tests, or needed by near-term editor workflows. Defer unsupported or rarely used legacy formats instead of adding decoders here.

## Required changes
- [ ] Define a small fixture matrix from current supported formats and workflow needs; do not include a format only because legacy handled it.
- [ ] Add runtime import smoke coverage proving decoded payloads materialize renderable/selectable `GeometrySources` or texture assets as appropriate.
- [ ] Add material re-resolution after texture upload/reload so model-scene material bindings observe child texture asset readiness.
- [ ] Add diagnostics that distinguish decode failure, materialization failure, upload deferral, and visual-readback failure.
- [ ] Keep broad GPU/Vulkan readback proof opt-in and skip-safe on hosts without an operational Vulkan lane.

## Tests
- [ ] Add CPU/null import materialization tests for each fixture domain.
- [ ] Add `contract;runtime` material-binding re-resolution tests after texture Ready/reload events.
- [ ] Add an opt-in `gpu;vulkan` sandbox/file-backed visual smoke for at least one representative model-scene plus standalone mesh/graph/point-cloud cases.

## Docs
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` with the coverage matrix and any deferred formats.
- [ ] Update `tasks/backlog/assets/README.md` and `tasks/backlog/README.md`.
- [ ] Document fixture provenance and size policy near the tests or in `tests/README.md` if labels change.

## Acceptance criteria
- [ ] Every format in the selected fixture matrix has a deterministic promoted import result and test evidence.
- [ ] Material texture bindings can re-resolve after child texture upload/reload without rerunning legacy import code.
- [ ] GPU/Vulkan visual proof is opt-in, labelled, and not part of the default CPU gate.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Asset|Import|Sandbox|Model|Texture' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding large third-party datasets to the default test gate.
- Importing legacy IO modules in new tests.

## Maturity
- Target: `CPUContracted` for the full fixture matrix; `Operational` only for explicitly labelled GPU/Vulkan smokes.
