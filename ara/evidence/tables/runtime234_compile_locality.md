# Point-analysis compilation locality — local diagnostic

RUNTIME-234 applies the existing processing-family boundary to density weights,
keypoints and outlier analysis. It reuses the shared command handle, property
capture, radius continuation and validated config application. Kernels and
backend choices are unchanged. Outlier-removal history now rejects an expired
attachment before dereferencing its scene.

## Matched public-record edit

Both variants prepend the same bool member to `EditorDensityWeightResult`, build
`IntrinsicRuntimeContractTests` and `ExtrinsicSandbox`, then restore the exact
source bytes and rebuild. The record lives in the broad processing interface
before the change and in PointAnalysisOperations afterward.

| Variant | Build elapsed | Physical compiler invocations | Production / test invocations |
| --- | ---: | ---: | ---: |
| Before | 351.986 s | 60 | 34 / 26 |
| After | 70.610 s | 16 | 12 / 4 |

The after sample rebuilt the three owning adapters, their family interface,
config and frame implementation, session composition, five app units and four
contract-test producers. Density/spacing, shared capture/radius code, mesh,
normal, descriptor, construction and unrelated workspace implementations were
not rebuilt. Four CTests independently enforce the intended compiler-module
boundaries, including shared compiled owners and migrated test producers.

Conditions: Clang 23, preset `ci`, Debug, eight build jobs, compiler cache disabled,
shared preinstalled dependencies. Exactly one sample per variant, ordered before
then after; filesystem caches were not flushed and host contention was not
controlled. Source is a dirty worktree. These are diagnostic observations, not a
repeatable speedup claim or a whole-engine clean-build measurement. Canonical
result payloads are explicitly not claim-eligible. Successful restoration took
370.709 s before and 65.380 s after; these are restoration checks, not additional
measured samples.

## Production footprint

All 32 changed production source/header/module/CMake files are counted against
the frozen pre-RUNTIME-234 checkout, preserving earlier uncommitted work.
Physical lines: 25,521 → 25,551 (**+30**). Nonblank lines: 24,265 → 24,284 (**+19**).
Three files were added for the coherent family; none were deleted. Deleting
five repeated config-apply bodies and catalog/forwarding duplication offsets
most of the explicit family-boundary and integration cost. This slice improves
locality and reduces repeated mechanisms; it is not a net source-size reduction.

## Review and evidence

Claude reviewed a fixed packet with tools, hooks and MCP disabled. Fixes cover
GPU test handle conversions, missing direct imports, copied catalog references,
callback-contract documentation/tests, invalid catalog inputs, selection clearing
through prepared frames and compiler gates for migrated test producers. The
source-only final review also checks that both shared compiled owners remain
independent of both families. Engine/config/UI behavior remains covered through
existing public operations and UI-action tests; this does not close the separate
Framework24 product-convergence gate.

The machine-readable companion records all changed production files, source
hashes, compiler edges, manifest-bound canonical results and review hashes.
Full local logs, fixed review and edit-probe scripts are archived under
`build/analysis/point-analysis-locality-2026-09-13/` (ignored build evidence).

## Final verification

`ci` builds `IntrinsicTests` and `ExtrinsicSandbox`; fresh `ci-asan` and
`ci-ubsan` build `IntrinsicCpuTests` with replacement-only grouped registration;
`ci-vulkan` builds `IntrinsicPointLBVHGpuTests`. Compiler cache is disabled.

| Gate | Selected CTest entries | Passed | Expected skips | Elapsed |
| --- | ---: | ---: | ---: | ---: |
| Final focused reconciliation | 157 | 157 | 0 | 8.97 s |
| Full CPU | 4,548 | 4,542 | 6 | 116.51 s |
| Full ASan | 2,926 | 2,926 | 0 | 548.33 s |
| Full UBSan | 2,926 | 2,925 | 1 | 241.08 s |
| Affected actual Vulkan integration | 22 | 22 | 0 | 192.40 s |

All selected gates have zero failures. Sanitizer CTest runs are serial; grouped
entries are not directly comparable with the individual CPU entry count. CPU
skips five native-window fixtures and the ASan-only leak control; UBSan skips
only that leak control. The Vulkan selector covers point LBVH and construction
smokes, including migrated weights, keypoints and outliers. It is not the entire
Vulkan test suite. Sanitizer discovery uses the authorized host path for existing
BUG-188. Final focused reconciliation ran after exact edit-probe source restoration.

Strict layering, task policy, doc links, test layout, skill mirrors, module
inventory freshness and compiler-boundary checks pass. Compile-hotspot tooling
tests (22) and both manifest-bound result validators pass. The existing strict
root-hygiene failure for `.agents/` remains tracked by BUG-177; this gate was not
weakened or represented as passing.

See the [machine-readable companion](../diagnostics/runtime234_compile_locality.json)
for exact test selectors, skip names, source and log hashes. RUNTIME-235 retains
the mesh-family migration and behavioral comparison/removal of the unused
immediate outlier API. The checkout remains uncommitted; this report closes
neither that work nor the Framework24 product gate.
