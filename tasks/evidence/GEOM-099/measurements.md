# GEOM-099 matched compile cost

Six alternating samples, three per arm, using the frozen manifest
[`geom099_reuse_compile.yaml`](../../../benchmarks/ci/manifests/geom099_reuse_compile.yaml).
Clang 23, ci-derived Debug, Null/headless, four jobs, tests enabled,
benchmarks disabled, ccache disabled, identical preinstalled dependencies and
source/build paths; no competing builds or tests during timing. Input pre-read,
no cache flushing or CPU/governor control. These are local acceptance measurements,
not a general performance improvement claim.

Source before: 14af21fbaf3590cb96df7f478fd94247224facf3

Source after: 1c1903c66a2866ed3cb55097a8bd1e9e71156a22

| Probe | Wall median seconds, before → after | Wall delta | Compiler sum median seconds, before → after | Compiler delta | Compiler jobs, before → after |
| --- | --- | --- | --- | --- | --- |
| clean | 185.643 → 186.083 | +0.24% | 713.672 → 715.643 | +0.28% | 395 → 395 |
| noop | 0.069 → 0.066 | -4.38% | 0.000 → 0.000 | n/a (zero before) | 0 → 0 |
| bff_impl | 2.914 → 3.003 | +3.07% | 1.465 → 1.455 | -0.68% | 1 → 1 |
| harmonic_impl | 2.320 → 2.787 | +20.14% | 0.863 → 1.325 | +53.53% | 1 → 1 |
| lscm_impl | 3.238 → 3.230 | -0.25% | 1.781 → 1.757 | -1.35% | 1 → 1 |
| utils_interface | 90.406 → 90.767 | +0.40% | 342.059 → 343.552 | +0.44% | 84 → 85 |

Per-sample values and min/max ranges are in `compile-summary.json`; all original logs, Ninja timings, configure state, dependencies and canonical results are in `raw-evidence.tar.gz`.

Original threshold crossings: harmonic_impl.wall_ms, harmonic_impl.compiler_ms

implementation: 4585 → 4477 physical lines (-108).

regression_tests: 1326 → 1523 physical lines (+197).

total: 5911 → 6000 physical lines (+89).


The operator superseded GEOM-099's automatic 2% rejection before after-arm
measurements. Accept the shared owner: three repeated traversal bodies become
one compiled function with an explicit live-seed contract, retaining each
solver's boundary checks and errors. This decision includes the measured
Harmonic implementation-edit cost (+467 ms wall, +462 ms compiler duration).
BFF's +89 ms median wall change remains below the original 100 ms floor.
The other three tasks retain their original limits.

The archived pre-edit pilot is separate from the six matched samples. All
pilot/setup attempts remain in the archive, including the missing-dependency
setup and the refused pre-existing build directory; neither contributes to the
matched medians. Canonical validation passed for all six final result records.

[`sources.bundle`](sources.bundle) preserves both measured commits and the
original-implementation UBSan diagnosis commit. It requires the existing
`7bdffefb309d9a3fe1815e371948a899b4a975a1` repository history. Source identities,
individual compiler invocations, dependency hashes and every raw result remain
available in the archive; `SHA256SUMS.json` binds the retained evidence files.
