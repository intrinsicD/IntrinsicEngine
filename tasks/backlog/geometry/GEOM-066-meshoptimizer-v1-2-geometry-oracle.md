---
id: GEOM-066
theme: B
depends_on:
  - REVIEW-003
  - GRAPHICS-105
maturity_target: CPUContracted
---
# GEOM-066 — meshoptimizer v1.2 geometry oracle and adoption evidence

## Goal

- Decide, from reproducible quality, correctness, and runtime evidence, whether
  pinned meshoptimizer v1.2 earns a later production dependency for tangent
  generation, simplification, or meshlet construction.

## Non-goals

- No production tangent property, vertex-layout field, asset-import mutation,
  material/shader change, or tangent-space normal-map path.
- No silent duplicate/degenerate filtering. `Geometry.MeshSoup` validation and
  `Geometry.HalfedgeMesh.Repair` remain authoritative and diagnostic.
- No replacement or semantic change to the `GEOM-014` feature-aware
  simplifier.
- No `GpuGeometryRecord`, upload, culling, mesh-shader, cluster-LOD, RHI, or
  renderer work; archived `GRAPHICS-044` and `GRAPHICS-056` own those future
  consumers.
- No copy, install, include, or promise around the demo-only `clusterlod.h`
  sample. This task evaluates only APIs shipped by the public meshoptimizer
  v1.2 library.
- No generic geometry-backend interface, registry, service, or factory.

## Context

- Owner/layer: evidence tooling and geometry tests over public `geometry`
  APIs. Production `geometry` remains dependent only on `core` unless a later,
  separately reviewed adoption task is opened.
- Issue 445 highlights meshoptimizer v1.2's MikkTSpace-compatible tangent
  generator, revised tangent weighting, geometry filtering, faster vertex
  codecs, and cluster-hierarchy support. The local vcpkg catalog currently
  carries meshoptimizer 1.1.1, so v1.2 requires an explicit manifest override
  or repository overlay port for this evidence run.
- `GRAPHICS-105` and `RUNTIME-129` explicitly exclude MikkTSpace/tangent-space
  normal work. This task measures the primitive without reopening that current
  object-space-normal scope.
- `GEOM-014` is the canonical in-engine simplification baseline. Meshoptimizer
  must be compared at equal face budgets using both quality and runtime
  metrics; a faster but materially different result is not a win.
- Sources: [Graphics Programming Weekly Issue 445](https://www.jendrikillner.com/post/graphics-programming-weekly-issue-445/),
  [meshoptimizer v1.2 release](https://github.com/zeux/meshoptimizer/releases/tag/v1.2),
  and the [meshoptimizer repository](https://github.com/zeux/meshoptimizer).
- Dependency policy: `AGENTS.md`'s current vcpkg-manifest-only rule supersedes
  the historical FetchContent/`external/cache` text in archived
  `GRAPHICS-044` and `GRAPHICS-056`.
- The Issue 445 cluster-hierarchy demo is not a library contract. Any later
  hierarchy experiment must implement its own bounded records over public v1.2
  library outputs rather than depending on `clusterlod.h`.

## Right-sizing

- Element under evaluation: one pinned third-party library, linked only by a
  focused evidence target; no engine-facing abstraction is introduced.
- Simpler alternative: fixed fixtures call meshoptimizer directly and compare
  it with existing public geometry APIs and checked reference outputs.
- Blast radius: `vcpkg.json`/overlay metadata, one evidence target, fixtures,
  tests, benchmark manifest/runner, and evidence docs; no production module
  import edge.
- Reintroduction trigger: only a separately opened consumer task backed by a
  passing adoption report may link meshoptimizer from production code.

## Required changes

- [ ] Pin meshoptimizer v1.2 through `vcpkg.json` plus an override or repository
      overlay port; do not use FetchContent, ad-hoc downloads, submodules, or
      `external/cache`, and expose only the public library target/headers to the
      evidence executable.
- [ ] Add tangent fixtures covering ordinary UVs, mirrored UVs, hard normals,
      UV seams, bevels, shared-position corners, and degenerate triangles.
      Compare exact-compatibility mode with checked MikkTSpace reference data,
      and record unit length, normal orthogonality, handedness, seam splits,
      determinism, and runtime for the revised weighting mode.
- [ ] Compare meshoptimizer simplification with `GEOM-014` at identical face
      budgets on fixed smooth, sharp-feature, boundary, and UV-seam fixtures.
      Emit surface-distance, feature/corner error, boundary/seam preservation,
      output counts, and runtime; do not collapse the two algorithms behind a
      new selector.
- [ ] Build meshlets for fixed indexed meshes, decode every local vertex and
      primitive list, and prove an exact source-triangle multiset round-trip,
      deterministic ordering, valid local indices, and declared size limits.
- [ ] Freeze a tangent adoption gate before measurement: every compatibility
      fixture must match checked MikkTSpace data within `1e-4` radians maximum
      angular error, match handedness and seam-split classifications exactly,
      emit no non-finite/zero-length frame, reproduce bitwise across runs, and
      take no more than `1.10x` the named MikkTSpace baseline median time on
      each of at least two nontrivial fixtures. Otherwise tangents are rejected.
- [ ] Freeze a simplification adoption gate before measurement: at identical
      output face counts, surface-distance, sharp-feature, boundary, and UV-seam
      error must each be no worse than `1.05x` `GEOM-014` on every fixture;
      median runtime must be at most `0.80x` `GEOM-014` on at least two
      nontrivial fixtures and no worse than `1.05x` on any fixture. Otherwise
      simplification is rejected.
- [ ] Freeze a meshlet adoption gate before measurement: source-triangle
      multiset round-trip, index/size validity, and determinism are mandatory;
      against a declared sequential-packing baseline, total local-vertex
      references must be at most `0.90x` on at least two nontrivial fixtures,
      no fixture may exceed `1.05x`, and median construction time may not exceed
      `1.10x`. Otherwise meshlet construction is rejected.
- [ ] Exercise v1.2 filtering only as an observed oracle result. Compare its
      removals with existing validation/repair diagnostics and record every
      disagreement; never apply the filtered result to an imported asset.
- [ ] Add a stable manifest-backed evidence workload
      `geometry.meshoptimizer_v1_2.oracle.smoke` with declared fixtures,
      parameters, warmup policy, baseline implementations, quality metrics,
      runtime metrics, and machine-readable diagnostics.
- [ ] Record independent `tangent`, `simplification`, and `meshlet`
      adopt/reject verdicts by mechanically applying the frozen numeric gates.
      Filtering receives observations only, not an adoption verdict. Production
      use remains a separate task; rejected operations leave existing paths
      authoritative and remove their unused dependency wiring.

## Tests

- [ ] Unit tests cover tangent invariants/reference compatibility, exact
      meshlet round-trip, invalid/degenerate inputs, and repeatability.
- [ ] Simplification comparison proves equal requested/output budgets and
      reports quality deltas alongside timings.
- [ ] The PR-fast smoke uses only checked-in/builtin fixtures; broader timing
      sweeps are marked `slow` or routed to nightly work.
- [ ] Benchmark manifest and emitted result JSON validate, and every runtime
      claim cites a named baseline and host/toolchain conditions.

## Docs

- [ ] Add a bounded evidence report under `docs/reports/` recording the pinned
      version, fixtures, MikkTSpace reference provenance, baseline conditions,
      quality/runtime results, limitations, frozen per-operation thresholds,
      and independent adopt/reject decisions.
- [ ] Update geometry benchmark documentation for the stable benchmark ID and
      explicitly state that no production tangent or repair path landed.

## Acceptance criteria

- [ ] Tangent, simplification, filtering, and meshlet observations are
      deterministic and traceable to fixed fixtures and machine-readable
      output.
- [ ] Meshlet decoding reproduces the source triangle multiset exactly; all
      simplification comparisons include quality metrics at equal budgets.
- [ ] The report mechanically applies the frozen tangent, simplification, and
      meshlet thresholds and records an independent falsifiable verdict for each
      operation without claiming production integration.
- [ ] No production module links meshoptimizer, no imported asset is silently
      repaired, no tangent storage or shader contract is added, and no
      `clusterlod.h` demo dependency is installed or copied.
- [ ] The default CPU gate and structural checks pass.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'Meshoptimizer|Simplification' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- FetchContent, direct network fetches, untracked binaries, or dependencies
  outside the repository vcpkg manifest/overlay path.
- Silent topology repair/filtering or mutation of normal import behavior.
- Production tangent storage, tangent-space shading, meshlet GPU records, or
  renderer/RHI changes.
- Installing, copying, including, or treating demo-only `clusterlod.h` as a
  supported meshoptimizer library surface.
- Performance claims without equal-quality baseline evidence.
- Mixing mechanical moves or unrelated geometry work into this evidence task.

## Maturity

- Target: `CPUContracted`; this task is an evidence oracle and adoption
  decision, not a runtime capability. No `Operational` follow-up is owed.
