# GEOIO-004 matched compile cost

Six alternating samples, three per arm, use the frozen manifest
[`geoio004_reuse_compile.yaml`](../../../benchmarks/ci/manifests/geoio004_reuse_compile.yaml).
Clang 23, ci-derived Debug, Null/headless, four jobs, tests enabled,
benchmarks disabled, ccache disabled, identical preinstalled dependencies and
source/build paths; no competing builds or tests during timing. Input pre-read,
no cache flushing or CPU/governor control. These are local acceptance measurements,
not a general performance improvement claim. The measured producer is
`IntrinsicGeometryIoTests`, corrected from the planning note before baseline
because it owns the actual IO test source.

Source before: 6975abfc14c5e5b55f62de77b5b022d1f9eda5b6

Source after: 478e32e0a27b9f7569710d8ddd0f503c5840d60a

| Probe | Wall median seconds, before → after | Wall delta | Compiler sum median seconds, before → after | Compiler delta | Compiler jobs, before → after |
| --- | --- | --- | --- | --- | --- |
| clean | 99.580 → 99.796 | +0.22% | 358.268 → 358.811 | +0.15% | 297 → 298 |
| noop | 0.058 → 0.059 | +2.72% | 0.000 → 0.000 | n/a (zero before) | 0 → 0 |
| mesh_loader | 2.532 → 2.518 | -0.54% | 2.020 → 2.016 | -0.20% | 1 → 1 |
| cloud_loader | 2.016 → 2.005 | -0.57% | 1.523 → 1.508 | -0.98% | 1 → 1 |
| private_header | 2.571 → 2.654 | +3.20% | 4.967 → 6.174 | +24.30% | 3 → 4 |

Per-sample values and min/max ranges are in `compile-summary.json`; all original logs, Ninja timings, configure state, dependencies and canonical results are in `raw-evidence.tar.gz`.

Original threshold crossings: private_header.compiler_ms

implementation: 5921 → 5847 physical lines (-74).

regression_tests: 6119 → 6269 physical lines (+150).

total: 12040 → 12116 physical lines (+76).

## Decision: rejected under the frozen stop rule

The private-header compiler-duration median rises from 4.967 to 6.174 seconds
(+24.30%), exceeding the frozen 2% allowance. Baseline samples span
4.961–4.983 seconds; candidate samples span 5.969–6.187 seconds. Even the
fastest candidate exceeds the slowest baseline plus its allowance, so this is
a confirmed regression, not an ambiguous threshold crossing. The new ordinary
parser translation unit increases observed header fan-out from three sources
to four. All other matched probes stay within their frozen allowances; the
header's +83 ms wall change stays within its 100 ms floor.

The candidate is rejected. All GEOIO-004 source, CMake, owner-route and regression
test additions were restored to the pre-task source. The seven characterizations
remain inspectable in the archived patch and source bundle. Retained code-size
change is **zero**; the -74 implementation / +150 tests / +76 total figures above
describe the rejected candidate. The complete `src`, `tests` and `cmake` trees
match restored source `a4509e0e115c9bca7c6d6fe0f23e7460bb3b1b50` byte-for-byte.
This rejection does not relax a threshold or propose a replacement design.

The before and after sources contain the same seven new public-loader
characterizations. Footprint accounting instead starts before those test
additions, so their 150 added lines remain visible separately. The ordinary
compiled parser adds one private source to the existing geometry library;
header-edit fan-out includes that new consumer. No module surface is added.

The pre-edit pilot and failed first extraction build are retained separately
from the six matched samples. The first build caught duplicate record insertion;
it was corrected before review or candidate timing. Claude then suggested only
moving the CMake entry beside the other IO source; final verification and timing
use that ordered entry. The source bundle preserves original, characterized,
failed, reviewed and final candidate commits, requiring existing history at
`563be93dba09b7b2a37062731aeafa252480e98e`.

The original and extracted loaders each pass all 224 GeometryIO cases; the
expanded locality selection passes 319. Full ASan passes all 3,237 selected
entries. Full ci (4,886 selected) and UBSan (3,237 selected) fail only on the
independently baseline-reproduced [BUG-206](../../done/BUG-206-uv-duplicate-submit-phase-race.md),
with the expected GLFW lifetime skip in those two runs. That check passes under
ASan. Neither failing run reports a sanitizer diagnostic. No unrelated fix or
weakened assertion is included. The independent review and compile measurements
do not imply that the unresolved repository gate is green.

After restoration, the canonical ci configure and IO target build pass; all 217 original GeometryIO cases pass. The restored source has zero task code changes.
