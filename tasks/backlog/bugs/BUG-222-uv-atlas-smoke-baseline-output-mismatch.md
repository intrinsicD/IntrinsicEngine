---
id: BUG-222
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive recording of an unrelated benchmark failure; no performance claim.
contract_schema: 1
contracts: []
contract_review: Benchmark baseline diagnosis under the existing benchmark workflow; no new engine or integration contract.
---
# BUG-222 — UV atlas smoke baseline output mismatch

## Goal

Diagnose the output-contract mismatch in
`geometry.uv_atlas.fast_staged_edge_grouping.scaling` without silently replacing
the baseline or weakening its quality gate.

## Evidence

Observed on 2026-09-25 at `6dd78b971` plus the property-smoothing and pre-existing
scalar-gradient/vector-field working changes. The all-workload
`IntrinsicBenchmarkSmoke` returned 2. Its new property-smoothing workload passed;
the atlas scaling workload failed its output signature / quality-vector contract.
The atlas kernel, workload and baseline were not changed by property smoothing.
This identifies an unrelated failing workload, not a proven root cause.

- Expected output signature: `5684639256857304174` (baseline `8ca52438`).
- Observed output signature: `13353140223134595692`.
- Quality-vector L2 difference: `0.38978686878190383`.
- Large-case topology is deterministic, finite and reports success; zero flipped
  faces. This does not satisfy the distinct baseline-output gate.
- [Sealed local result](../../evidence/BUG-222/uv-atlas-result.json.gz).
- Full local runner log: `/tmp/property-smoothing-benchmarks.log`.
- 2026-10-18: this workload is the only failure of `nightly-deep` "Run benchmark smoke"
  on every night since at least 2026-10-09. A local `build/ci` run at `3302d06e3` gives
  the same signature `13353140223134595692`: topology and counts equal the baseline;
  mean conformal distortion 1.18279 → 1.0000016, max stretch 0.70546 → 0.35485,
  `uv_max.y` 0.91768 → 0.98922 (planar 32×32 grid). Atlas-path commits after the
  baseline: `25d270681` (BUG-101; its report shows unchanged output), `1846a5692`,
  `985c7671e`, `9221c02c8`.

## Acceptance criteria

- [x] Reproduce on an isolated source revision and identify the first relevant atlas change.
- [x] Decide whether the output is a regression or an intentionally changed contract, with numerical evidence.
- [ ] Repair the implementation or update the reviewed baseline protocol without weakening unrelated gates.

## Diagnosis — 2026-10-19

Host `hochvogel`, clang++-23, `ci` preset (Debug). HEAD runs used `build/ci` at `c4f4ec100`;
older revisions were built in a separate worktree (`VCPKG_MANIFEST_INSTALL=OFF`, the main
checkout's installed packages; `vcpkg.json` changed only after 2026-10-07). The HEAD objective
probes temporarily changed only `options.Distortion` in `MakeOptions`
(`benchmarks/geometry/Bench_UvAtlasSmoke.cpp`), rebuilt, ran and restored it. Predictions were
fixed before each run; signatures are of the 32×32 workload.

| Run | Prediction | Outcome |
| --- | --- | --- |
| HEAD, unchanged (`Angle`) | new signature | `13353140223134595692`, failed — holds |
| HEAD, `Distortion = None` | baseline signature | `13147532661243740981`, 158 charts, conformal 3.43 — refuted |
| HEAD, `Area` / `Both` | one of them baseline | `1578760485508691887` / `11944554893966438409`, conformal 1.0000013 / 1.0000016 — refuted |
| `c97235568` (= `9221c02c8^`) | baseline signature | `5684639256857304174`, passed, quality L2 0 — holds |
| `9221c02c8` | new signature | `13353140223134595692`, failed — holds |

- First relevant change: `9221c02c8` ("Implement property-guided UV atlases and split atlas
  inspection", METHOD-047). It replaced the FastStaged per-chart solve in
  `Geometry.UvAtlas.cpp` (LSCM, then uniform square-boundary Tutte, then planar projection)
  with `Geometry.UvAtlas.ChartSolve.cpp` and explicit objectives (Angle = LSCM, see
  `docs/methods/property_guided_atlas.md`), and changed packing (area-normalized charts, one
  reserved texel per shelf cell). The new `Distortion` option alone does not explain it: no
  objective at HEAD reproduces the old output, and HEAD still emits the `9221c02c8` signature.
- Verdict: an intended solver/packing change left the frozen output baseline stale; it is not
  an output regression on this fixture. Topology is identical (1089 vertices, 2048 faces, one
  chart, no seams, 128 boundary seams, no flips, no fallback); mean conformal distortion
  1.18279 → 1.0000016 and max stretch 0.70546 → 0.35485; UV extents differ through packing.
  Large-grid median runtime on this host 111.5 → 139.0 ms (Debug; this workload has no timing
  gate). One fixture and one host; no method-quality or performance claim.
- Why it stayed red: BUG-213 (`54fe0deb0`) restored the manifest byte-identical to the
  immutable `8ca52438` result (which binds its sha256) and kept Angle in `MakeOptions`, but the
  output signature and quality vector in that manifest and in `Bench.UvAtlasSmoke.hpp` remain
  the pre-`9221c02c8` values. A repair therefore needs an operator decision on the frozen
  binding (BUG-213 said not to rewrite the baseline or hash).

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
python3 tools/benchmark/run_and_seal.py --executable build/ci/bin/IntrinsicBenchmarkSmoke --output /tmp/bug222-benchmarks --manifests-root benchmarks --run-id bug222-reproduction
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/bug222-benchmarks --manifests-root benchmarks --strict
```
