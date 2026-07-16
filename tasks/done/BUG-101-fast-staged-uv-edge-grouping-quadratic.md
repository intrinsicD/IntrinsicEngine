---
id: BUG-101
theme: G
depends_on: []
maturity_target: Operational
---
# BUG-101 — Fast-staged UV edge grouping is quadratic

## Status

- Completed on 2026-07-16 at `Operational`; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`; implementation commit:
  `25d27068`.
- Deterministic geometry and real-runtime close coverage passed 3/3. The
  declared runner emitted 22 strictly valid results; the comparable local
  sanitized baseline/candidate evidence records exact output parity and a
  46.175% lower large-fixture median without turning local timing into a gate.
  The default CPU-supported gate passed 3,830/3,830.

## Goal

- Make fast-staged UV source-edge grouping scale near-linearly with triangle
  incidences while preserving deterministic chart/seam output, so a dense
  direct-mesh enrichment cannot keep Sandbox shutdown waiting for minutes.

## Non-goals

- No fixture-specific bypass, default-method switch, or disabling imported
  mesh UV generation.
- No claim that stale async completion is safe; `BUG-095` owns generation
  validation and pending action readiness.
- No broad streaming-executor cancellation redesign in the localized fix.
- No checked-in dependency on `child.obj` or wall-clock GTest threshold tuned
  to one workstation.

## Context

- Symptom: importing local `child.obj` succeeds visibly, but one worker stays
  CPU-hot and a correct window-close event leaves the process/window alive
  while `StreamingExecutor::ShutdownAndDrain()` waits for that work.
- Expected behavior: the default fast-staged atlas path handles a 100k-face
  closed mesh without quadratic edge-key lookup or redundant regrouping; a
  completed close should not be dominated by avoidable atlas work.
- Root cause: `BuildSourceEdgeFaceGroups()` performs a vector-wide
  `find_if()` for each of `3F` incidences. The observed dataset has 150,000
  unique edges, implying at least 11.25 billion comparisons on the first
  grouping alone. `RecordFastStagedSeams()` rebuilds the same groups, doubling
  that class of work.
- Owner: private `geometry` UV-atlas implementation. Runtime remains the
  consumer and retains its intentional drain-before-destruction policy.
- A separate cooperative-cancellation follow-up is warranted only if bounded
  close still fails for legitimately long non-quadratic jobs after this fix.

## Required changes

- [x] Replace the vector-wide edge search with a reserved edge-key-to-index
      lookup that preserves first-seen group order and per-group face order.
- [x] Use a deterministic normalized undirected edge key with collision-safe
      equality; do not derive output order from hash-table iteration.
- [x] Reuse the already-computed source edge groups when recording fast-staged
      seams instead of rebuilding them.
- [x] Preserve boundary/nonmanifold classification, chart membership, seam
      records, diagnostics, UV finiteness, and authored-UV behavior.

## Tests

- [x] Add generated high-cardinality manifold/grid coverage asserting
      fast-staged success, face preservation, finite UVs, and deterministic
      chart/seam output across repeated runs.
- [x] Pin boundary and nonmanifold edge grouping so the faster lookup cannot
      merge unequal keys or reorder first-seen records.
- [x] Extend the declared UV-atlas benchmark with a scaling fixture large
      enough to detect quadratic grouping and compare the candidate result to
      a recorded baseline before making a performance claim.
- [x] Add a bounded real-engine direct-mesh enrichment/close regression using
      a generated fixture if the existing runtime seam can prove operational
      shutdown without a brittle wall-clock assertion.

## Docs

- [x] Update the geometry UV-atlas notes and benchmark manifest/README for the
      new scaling evidence.
- [x] Update runtime docs only if shutdown/cancellation semantics change.
- [x] Refresh task indexes/session brief and retirement records on closure.

## Acceptance criteria

- [x] Edge grouping performs expected constant-time key lookup per incidence
      and is constructed once per fast-staged generation.
- [x] Deterministic chart/seam/UV results match the pre-fix semantics on
      representative boundary, manifold, and nonmanifold inputs.
- [x] A declared benchmark result demonstrates the dense-fixture scaling
      change against an explicit baseline; no unsupported speedup claim is
      recorded.
- [x] Generated operational coverage no longer leaves close waiting on the
      former avoidable quadratic worker, and focused/default gates pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicBenchmarks
ctest --test-dir build/ci --output-on-failure \
  -R '^(UvAtlas\.(FastStagedHighCardinalityGridIsFiniteAndDeterministic|FastStagedKeepsFirstSeenBoundaryAndNonmanifoldSeamOrder)|RuntimeAssetImportFormatCoverage\.DirectMeshEnrichmentCloseDrainsGeneratedGridAndCompletesDeterministically)$' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark --strict
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Verification completed on 2026-07-16. Clean detached baseline
`8ca524387a92ea444b63fb28cac723ca4c9feebb` and candidate measurements used
the same `ci-vulkan` Clang 23 Debug ASan/UBSan configuration, serialized idle
host, and byte-identical harness. The large-fixture median changed from
1,031.895895 ms to 555.416594 ms (46.175% lower), normalized scaling changed
from 1.726739 to 1.195250, quality L2 remained zero, and the output signature
matched exactly (`5684639256857304174`). These are scoped local sanitized
measurements, not a broad performance claim or timing gate. All 22 emitted
benchmark results validated strictly; the default CPU-supported gate passed
3,830/3,830.

## Forbidden changes

- Iterating hash-table order to produce chart or seam records.
- Replacing the default algorithm merely to avoid the hot loop.
- Adding a wall-clock-only unit assertion with no declared benchmark evidence.
- Folding stale-completion semantics from `BUG-095` or a generic executor
  cancellation API into this localized geometry fix.

## Maturity

- Achieved: `Operational`: geometry correctness and declared scaling evidence
  establish `CPUContracted`; a generated real runtime enrichment/close path
  proves the fix reaches the workload that exposed it. No Vulkan proof is
  required because the hot work is CPU-only.
