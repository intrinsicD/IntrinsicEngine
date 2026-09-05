# BUG-159 atlas remap regression diagnostics

This is a local software-regression record for BENCH-001's resident-input UV
enrichment boundary. Both result payloads are `local-dev`,
`claim_eligible: false`. They do not establish Framework24 parity,
import-to-visible latency, a Release speedup, or a product scorecard result.

## Reproduction and repair

The original FastStaged implementation retained a source-vertex-sized lookup
in every chart, copied that lookup while validating solver UVs, and allocated
another source-sized lookup during output assembly. The repair removes the
retained field, uses one source-to-local scratch vector, clears only touched
entries before solver early returns, and assembles output through the existing
chart-local vertex and corner order. Chart admission, packing, solvers, and
backend/fallback policy are unchanged.

The isolated `IntrinsicUvAtlasRemapSmoke` executable calls the public CPU
`ResolveUvAtlas` API. Its scoped replacement of ordinary `new`/`new[]`
counts allocation traffic during that synchronous call. It does not count
aligned allocation, native `malloc`, fixture construction, snapshot
serialization, or output IO; the count is neither live storage nor peak RSS.
A separate executable prevents this probe from affecting other tests.

The frozen fixture contains 512 disconnected triangles, 65,536 source
vertices, nonconsecutive face indices, unused vertices, and a copied source-ID
property. One warmup precedes three measured calls. All use FastStaged with
fallback disabled, resolution 1024, and padding 2. The independent 1,024-chart
GoogleTest fixture checks every source reference and copied property plus
complete faces, finite normalized UVs, and repeatability.

## Local observations

Collected on 2026-09-05 using the unsanitized `ci` Debug preset and Clang
23.0.0. Baseline and candidate ran serially after their builds completed.
The baseline geometry source is unchanged from `e6350ca56`; both use the
same added harness. Each result records the exact geometry-source and runner
SHA-256, manifest hash, resolved parameters, and raw samples.

| Observation | Original implementation | BUG-159 candidate |
| --- | ---: | ---: |
| Counted bytes, run 1 | 408582739 | 6179411 |
| Counted bytes, run 2 | 408582739 | 6179411 |
| Counted bytes, run 3 | 408582739 | 6179411 |
| Allocation-budget outcome (67108864 bytes) | failed | passed |
| Median scoped time, Debug milliseconds | 104.593061 | 49.745099 |
| Finite, complete, deterministic output | true | true |

Timing is retained as a diagnostic; no speedup conclusion is drawn from this
Debug run. The allocation budget is deliberately loose, with no runtime
threshold. It was specified before applying the production change.

- [Original schema-v2 result](../../benchmarks/baselines/geometry_uv_atlas_remap_e6350ca56.json)
- [Candidate schema-v2 result](../../benchmarks/results/geometry_uv_atlas_remap_bug159.json)
- [Frozen manifest](../../benchmarks/geometry/manifests/geometry_uv_atlas_fast_staged_remap_smoke.yaml)
- [Exact output snapshot, gzip](../../benchmarks/baselines/geometry_uv_atlas_remap_output.snapshot.gz)

The decompressed snapshot is byte-identical before and after the repair:
`179b328feb2fedc9aec6eda1d7d75b1a72bbfd86a971f88e010c839ce412507d`
(SHA-256). It contains the many-chart fixture plus the existing square, cube,
and nonmanifold-edge fixtures: positions, triangle indices, UVs, copied
properties, source cross-references, chart/seam records, all exposed atlas
diagnostics, and per-chart/global parameterization diagnostics. Small-fixture
runs are untimed and are repeated for exact equality. Snapshot equality is
local before/after evidence, not a cross-toolchain floating-point guarantee.

## Invocation

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicUvAtlasRemapSmoke
python3 tools/benchmark/run_and_seal.py \
  --executable build/ci/bin/IntrinsicUvAtlasRemapSmoke \
  --output /tmp/bug159-check/result.json \
  --manifests-root benchmarks --run-id bug159-check --attempt-id attempt-001
ctest --test-dir build/ci --output-on-failure -R UvAtlas --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
```

The runner writes a sibling `result.json.snapshot`; compare its bytes or
SHA-256 with the decompressed checked-in snapshot for the recorded configuration.
For a before/after reproduction, apply only the same benchmark harness to
`e6350ca56`, then repeat on the implementation revision recorded in the
[task](../../tasks/backlog/bugs/BUG-159-fast-staged-atlas-per-chart-global-remaps.md).

## Evidence limits and remaining work

The regression executes the built-in CPU FastStaged path. It adds no GPU
evidence and does not measure parsing, runtime publication, rendering, or
end-to-end import. BENCH-001 retains representative matched product timing and
memory work. BUG-160 retains the separate chart-fragmentation defect.
