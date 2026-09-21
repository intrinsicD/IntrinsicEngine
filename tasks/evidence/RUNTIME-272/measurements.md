# RUNTIME-272 matched compile cost

Six alternating samples, three per arm, use the frozen manifest
[`runtime272_reuse_compile.yaml`](../../../benchmarks/ci/manifests/runtime272_reuse_compile.yaml).
Clang 23, ci-derived Debug, Null/headless, four jobs, tests enabled,
benchmarks disabled, ccache disabled, identical preinstalled dependencies and
source/build paths; no competing builds or tests during timing. Input pre-read,
no cache flushing or CPU/governor control. These are local acceptance measurements,
not a general performance improvement claim.

Source before: 549c2d4b70a64a5bfeb8f1e6dd636fc86ee706f2

Source after: 5f2f99ef0f5c05218ca4ed67b28da87ee9a2df32

| Probe | Wall median seconds, before → after | Wall delta | Compiler sum median seconds, before → after | Compiler delta | Compiler jobs, before → after |
| --- | --- | --- | --- | --- | --- |
| clean | 421.092 → 420.751 | -0.08% | 1626.390 → 1626.102 | -0.02% | 871 → 872 |
| noop | 0.102 → 0.103 | +1.29% | 0.000 → 0.000 | n/a (zero before) | 0 → 0 |
| consumer_impl | 11.816 → 11.730 | -0.73% | 7.545 → 7.373 | -2.28% | 1 → 1 |
| fixture_header | 58.122 → 22.186 | -61.83% | 200.903 → 62.217 | -69.03% | 20 → 10 |
| new_fixture_impl (after-only) | 5.969 | supplemental | 1.698 | supplemental | 1 |

Per-sample values and min/max ranges are in `compile-summary.json`; all original logs, Ninja timings, configure state, dependencies and canonical results are in `raw-evidence.tar.gz`.

Original threshold crossings: none

implementation: 7099 → 6923 physical lines (-176).

regression_tests: 0 → 0 physical lines (+0).

total: 7099 → 6923 physical lines (-176).

All matched medians pass the original frozen allowances. No speedup claim is made; the clean-build difference is negligible and sample ranges overlap.

The declaration probe deliberately compares different scopes: the baseline
`EditorFeatureTestContext.hpp` is broad; the candidate's new
`PointDomainFixture.hpp` is narrow. The duplicated topology did not previously
live in the broad header. Compiler-job counts report the actual consequence
of editing each declaration surface. The new `PointDomainFixture.cpp` probe
is an additional after-only cost; its before arm is a settled no-op and is
excluded from matched regression gates.

All samples, including the separate pre-edit pilot, are retained. Exact compiler
invocations, dependency fingerprints and canonical results remain in the raw
archive. The source bundle preserves the before, reviewed candidate and failed
first-build snapshots and requires repository commit
`66976e593b9562e5f0c34688e937d741016768cd`. The failed first build missed two
accessor callers; the corrected candidate was reviewed and fully reverified
before any after-arm timing. No source changed during the six matched samples.

The full ci suite selected 4,879 tests with zero failures; full ASan selected
3,230 with zero failures. The GLFW lifetime check skips in ci and passes under
ASan; all 3,230 ASan entries ran. Fresh
Clang 20 compiled the support producer and runtime executable, with 101 selected
contract cases passing and unchanged dependency fingerprints. The 226 focused
ci cases passed before and after extraction. Full UBSan failed only the
independently baseline-reproduced [BUG-206](../../backlog/bugs/BUG-206-uv-duplicate-submit-phase-race.md).
This repository-gate limitation remains explicit; the implementation review
and compile comparison do not imply that the task has retired.
