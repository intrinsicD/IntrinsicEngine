# GRAPHICS-146 matched compile cost

Six alternating samples, three per arm, use the frozen manifest
[`graphics146_reuse_compile.yaml`](../../../benchmarks/ci/manifests/graphics146_reuse_compile.yaml).
Clang 23, ci-derived Debug, Null/headless, four jobs, tests enabled,
benchmarks disabled, ccache disabled, identical preinstalled dependencies and
source/build paths; no competing builds or tests during timing. Input pre-read,
no cache flushing or CPU/governor control. These are local acceptance measurements,
not a general performance improvement claim. `IntrinsicTests` includes every
mock consumer, including runtime tests outside the focused graphics producer.

Source before: d82d87d765400961b1c2870fbf3d871eafdad2df

Source after: 33f6e4427c9ba3dbb050c5d81fe0e5d177ebf46a

| Probe | Wall median seconds, before → after | Wall delta | Compiler sum median seconds, before → after | Compiler delta | Compiler jobs, before → after |
| --- | --- | --- | --- | --- | --- |
| clean | 626.720 → 625.658 | -0.17% | 2423.576 → 2419.458 | -0.17% | 1151 → 1151 |
| noop | 0.138 → 0.133 | -3.51% | 0.000 → 0.000 | n/a (zero before) | 0 → 0 |
| consumer_impl | 2.607 → 2.544 | -2.42% | 1.735 → 1.685 | -2.88% | 1 → 1 |
| mock_impl | 5.588 → 5.518 | -1.24% | 1.041 → 1.011 | -2.88% | 1 → 1 |
| mock_header | 56.487 → 56.937 | +0.80% | 189.063 → 190.272 | +0.64% | 50 → 51 |

Per-sample values and min/max ranges are in `compile-summary.json`; all original logs, Ninja timings, configure state, dependencies and canonical results are in `raw-evidence.tar.gz`.

Original threshold crossings: none

implementation: 2014 → 1812 physical lines (-202).

regression_tests: 73 → 106 physical lines (+33).

total: 2087 → 1918 physical lines (-169).

The new field-contract case adds `MockRHI.hpp` to the existing
`Test.GraphicsTestSupport.cpp`, creating one additional declaration consumer.
Its observed rebuild cost remains included in the shared-header probe. The
separate regression-test footprint includes the test's two-line include addition;
the 17 strengthened assertions in migrated callers remain included in the
recorder/caller net change. No production source or CMake entry changes.

The pre-edit pilot is retained separately from the six matched samples. All
compiler invocations, wall timings, dependency fingerprints and canonical result
records remain in the raw archive. The source bundle preserves both exact
commits and requires existing history at
`1fd3b3e72ea4321ff063b1eb555a62d60bd9aec5`. No source changed after independent
review or between measured samples. No Vulkan execution claim is made by the
CPU mock tests.

## Behavioral verification

The original eleven focused graphics cases pass before extraction. The reviewed
candidate passes twelve focused cases, including the new recorder-contract test,
and the expanded graphics/compilation-locality selector passes 107 entries.
Canonical ci configuration and `IntrinsicTests` build pass; the full CPU selector
passes 4,880 selected entries with one expected GLFW lifetime skip (168.72 s).
Fresh grouped ASan configuration, `IntrinsicCpuTests` build and serial full gate
pass all 3,231 entries (657.23 s). Fresh grouped UBSan configuration, matching build
and serial full gate pass 3,231 selected entries with the expected GLFW lifetime
skip (294.23 s). No sanitizer findings occur.

These gates cover exact combined source
`33f6e4427c9ba3dbb050c5d81fe0e5d177ebf46a`: GEOM-099 and RUNTIME-272 retained,
GEOIO-004 restored, and this reviewed graphics candidate. All 23 code/build files
affected by GEOM-099 and RUNTIME-272 are byte-identical to their reviewed source
identities; the raw archive includes that per-file hash check. This is the required
verification for a new graphics diff, not a retry of unchanged source to obtain
a passing result. The previously recorded BUG-206 phase-snapshot race does not
occur in these runs; it remains an open independent bug and is not claimed fixed.

## Acceptance decision

Accepted: every original frozen median limit passes, with zero no-op compiler
work. The shared-header increase includes the added regression consumer; it is
not omitted from the measured footprint. All six dependency fingerprints match.
The first baseline sample is slower than subsequent samples; all runs remain
included in the prescribed median comparison and in the raw evidence. No cache,
job, compiler or source settings were changed after seeing results.

The complete implementation/declaration/caller footprint decreases by 202 lines;
the additional regression test adds 33 lines, for a total decrease of 169.
Claude Sonnet medium found no actionable implementation defects; its production
stream-audit limitation is explicit in [the review](implementation-review.md).
The passing required combined-source gates resolve the earlier GEOM-099 and
RUNTIME-272 verification obligations through the unchanged-file identity check.
BUG-206 remains open. This consolidation introduces no new engine capability,
backend execution claim or capability-maturity promotion.

## Repository closure checks

Strict layering, test layout, task policy/maturity, task-state links, relative
links, root hygiene, skill mirrors, ARA structure, Codex config and allowlist
checks pass. All 109 benchmark manifests and 17 method manifests validate;
workflow evidence reports zero errors and 105 existing historical warnings,
and all eight custody protocols/runs validate. The module inventory, skill
mirrors and session brief were regenerated. Whitespace checks pass. These
structural checks run after timing; they do not compete with compile samples.
