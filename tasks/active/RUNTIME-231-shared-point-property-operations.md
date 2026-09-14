---
id: RUNTIME-231
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive simplification; implementation evidence will be the combined diff and relevant CPU/GPU tests.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation, method.engine-integration]
---
# RUNTIME-231 — Share density and spacing property-operation mechanisms

## Goal
Prove that density and spacing can share their runtime mechanisms with net
production-code deletion, unchanged features and clear typed entry points.
The operator authorized implementation and reuse discovery on 2026-09-11.
The runtime pilot is implemented and reviewed; CPU, UI, actual Vulkan and
bounded compile checks passed. The note awaits commit/retirement. See the [review](../../docs/reviews/2026-09-11-processing-complexity-review.md).

## Scope and design
- Replace duplicate watch/domain helpers with the existing PointProperties owner.
- Share live-sample/deletion capture, framed kNN continuation and guarded scalar
  property publication using ordinary records and out-of-line free functions.
- Reuse JobService/JobDesc for CPU fallback, queued execution, cancellation and
  terminal delivery. Keep typed method validation, Compute, statistics and messages.
- Preserve public config/result contracts and serialized backend tokens; map to
  one private neighborhood policy if necessary. No new public module, service,
  registry, builder or header-defined method lifecycle framework.
- Scope is Density and Spacing. Topology replacement, in-place writes, multi-output
  methods and a generic result store require separate evidence before adoption.
- Extend existing private owners first. Any additional source unit needs a
  compile/ownership reason and must be counted in the net footprint.

## Engine integration
| Field | Preserved contract |
|---|---|
| Least-structured input | Typed float3 samples on the selected canonical property domain. |
| Compatible entity sources | All currently supported mesh, graph and cloud element domains, including deleted-row mappings. |
| RuntimeModule | Existing geometry operations, spatial cache and JobService; no new module lifecycle. |
| Config/agent | Existing density/spacing sections and shared validated preview/apply functions. |
| UI | Existing panels continue to work; UI-054 owns the shared panel scaffolding implemented with this pilot. |
| Publication | Named same-domain float outputs, preserved unrelated/deleted rows, revision-guarded history and dirty/cache notifications. |
| End-to-end tests | Existing density/spacing contract suites and their real Vulkan candidate-policy cases. |

## Acceptance criteria
- [x] Both methods call one owner for watches/domain resolution, captured live samples,
  kNN continuation and scalar history publication; their duplicate bodies are removed.
- [x] Total affected production lines decrease after counting all shared code and
  adapters. Record before/after counts and explain retained method-specific sequencing.
- [x] Preserve exact neighborhood width floors, self/coincident/tie policy, source-ID
  mapping, count/range caps, requested/actual reporting and no silent fallback.
- [x] Existing config, eight-domain, stale/cancel, output creation/removal, undo/redo
  and unrelated-property preservation tests pass. Add only missing coverage for
  deletion runs across batch boundaries and source replacement before undo.
- [x] CPU numerical outputs retain their existing behavior and GPU outputs satisfy
  existing tolerances. Verify actual Vulkan execution for the shared continuation.
- [x] Keep current mutation/history/notification ordering; any behavior change is
  separately reviewed. Do not impose an observer-order guarantee absent today.
- [x] Build IntrinsicTests and Sandbox; measure compilation impact using the same
  preset/toolchain/job budget. Investigate an unexplained regression before expansion.
- [x] Update the architecture note and inventory if ownership/surfaces change;
  independently review the final fixed diff before considering more methods.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -R '^(KernelDensity|PointSpacing|SandboxProcessingPanels)'
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.(KernelDensity|PointSpacing)PublishesAcrossDomainsAndPreservesCandidatePolicy$' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

Reintroduction trigger: split a shared mechanism when a demonstrated semantic
difference cannot be expressed as an ordinary input parameter without method-name
branches. Keep distinct kNN and radius query contracts. Do not generalize for a
hypothetical future method.

## Completion evidence

Implemented and reviewed on 2026-09-11; uncommitted, awaiting commit/retirement.
Runtime footprint: 966 → 853 physical lines, including the 47-line private
header. Typed job delivery stays in the method adapters. The
[review](../../docs/reviews/2026-09-11-processing-complexity-review.md#implemented-pilot-and-reuse-discovery)
records the completed CPU, UI, Vulkan and bounded compile checks and the
[reuse skill](../../tools/agents/skills/intrinsicengine-reuse/SKILL.md).

Exact commands, source hashes, Claude findings/fixes, footprint and validated
compile-probe manifests/results are retained locally under
`build/analysis/reuse-2026-09-11/`. Local configure/build commands used
`VCPKG_FORCE_SYSTEM_BINARIES=1`. No full-build speedup is claimed.
