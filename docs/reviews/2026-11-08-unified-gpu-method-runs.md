# Unified GPU method execution: verification and review

Scope: [ARCH-023](../../tasks/done/ARCH-023-unified-gpu-method-runs.md), the existing GPU/hybrid
methods and their CPU counterparts. This is an engineering refactor and deterministic contract
verification, not a new scientific result, a performance measurement or a cross-device guarantee.
The [coverage matrix](../methods/gpu-method-coverage.md) owns the current method inventory and
limitations; the [method template](../methods/method-template.md) owns the reusable pattern.

## Source and verification

The combined CPU/Vulkan run kept production source unchanged at Git tree
`ee4ed57b0cd4c9951d31993427ade413889cb63d`. The final scalar fixture correction and CTest
registration are in tree `cf1fa7f59b9842e389b70cbb5a11eb8f08df8c1c`; no runtime, kernel,
plugin or method source changed between them. The core implementation is committed as
`81269bd14630d4fc410d9397863bcbf8475f4251`; its CPU-budget correction is `c1fa06c32`.
Test counts below are verification results, not benchmarks.

- **CPU:** `cmake --build --preset ci --target IntrinsicTests`, then
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j4`:
  4,500 selected, zero failures; numeric-locale and GLFW/LSAN environment checks skipped.
  `SandboxEditorPresentation.SourceRules` also passed separately.
  PR CI subsequently caught missing CPU-slot declarations for two new CPD worker tests. Commit
  `c1fa06c32` adds those exact declarations and updates the source-derived golden without relaxing
  it. The check was reproduced red, then passed all 20 cases; generated CTest metadata reserves
  three slots for each test. The complete 4,500-case CPU selection passed again. Twenty CI tooling
  regression scripts also passed locally, and the updated documentation CI passed.
- **Vulkan:** all non-slow, non-quarantined cases owned by `IntrinsicGraphicsVulkanSmokeTests`,
  `IntrinsicPointLBVHGpuTests`, `IntrinsicRuntimeClusteringServiceGpuSmokeTests`,
  `IntrinsicRuntimeGpuResultReadbackSmokeTests`, `IntrinsicRuntimePointCloudConsolidationGpuParityTests`,
  `IntrinsicRuntimeProgressivePoissonResultGpuSmokeTests` and
  `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`, built with preset `ci-vulkan` (ASan and UBSan).
  NVIDIA RTX 3050, driver 590.48.01; the method lane used a separate queue, sharing neither the
  frame-compute queue nor the graphics queue lock. The 219-case run passed 217, skipped the
  opt-in CPD scaling profile, and timed out in one scalar fixture. That fixture had not requested
  Run after the common default became Ready. After asserting Ready and explicitly requesting
  Run through StepRuns, its unchanged parity/residency/discard checks passed five repetitions
  (`ctest --test-dir build/ci-vulkan --output-on-failure --timeout 60 -R '^RUNTIME298PointScalarResidency.ParityResidentSecondRunAndDiscard$' --repeat until-fail:5`).
  Thus 218 distinct GPU cases passed across the combined gate and focused correction. The
  pre-existing quarantined `GpuResultReadbackGpuSmoke.SharedBatchParksJobAndReleasesDerivedUpload`
  was excluded; no gate was weakened or new quarantine added.
- **Sanitized contracts:** 13 cases covering construction paging/abort/range refusal, CPD
  workspace reuse, held-page shutdown and cancellation-origin reporting passed in `ci-vulkan`.
- **Structural:** strict layering, allowlist quality, feature boundaries (R1 27/27 without
  raising the ceiling), task policy/state links, documentation links/sync, skill mirrors,
  ARA references and test layout passed. All 271 source-rule mutation self-checks passed.
- **Mechanisms:** compiled removal probes exercised execution limits, queue selection and
  ordering, cache retirement, settled callbacks, scalar policy, construction pages and abort,
  cancellation origin and abandoned owner hooks; each recorded failure was followed by a
  restored-green check. Unprobed cases are explicitly listed in the coverage matrix.
  The workspace target boundary was also tested with a forbidden real plugin import:
  baseline build passed, private-module import failed, restored source built again.
- **Duplication:** the whole-source CLI scan included byte-identical copies of all 481 `.cppm`
  files because the project MCP scan omits that extension. Changed production matches were
  inspected against the prior scan; typed binding glue reuses the shared owners. No semantic
  duplication model was used. The engine-only workspace factory exists once.

## Review and limits

Independent Opus block reviews covered the queue, run driver, query sessions, resident producers,
plugins, one-shot adapters, construction and ownership corrections. The fresh final reviewer
(session `3bed78aa-f0ae-4a1b-bf64-1407f84919d8`) inspected the shared mechanisms and cross-checked
family consumers against those reviews and approved the core at tree
`a85668cebdebad9186cb8643230b94f8ac6c6592`. Its explicit CPD-subsampling caveat was carried into
the final worker slice below, committed as `da3ca8b8a36234fb4847b4d73436a7da9f48da07`. On 2026-11-08 the same final
reviewer returned **“Final verdict: APPROVE.”** for tree `1ca7f8ed443bbd5afbe43ffe1e23ddb12f1d4c74`,
judging all nine acceptance criteria met and the Q9 CPD condition closed. Review packet SHA-256:
`ed054ba3b787f0001496176bbd6a9faf361cfed27fa63c425bf391196e48ff93`. Requested model/effort: Claude Opus 5.5/max; reported
model: `claude-opus-5-5` (effort not independently echoed). Read-only access and no further delegation
were enforced by the CLI tool whitelist. The verdict includes the bounded closure edits recorded here.

The architecture scorecard found layer imports, CMake links, downward-facing APIs, renderer
ownership and exception ownership sound. Typed pass IDs and new recipe edges were not applicable.
Maturity is **Operational**, bounded to these deterministic backend tests; scorecard row 7 passed. There are no new layer-policy exceptions.

[RUNTIME-355](../../tasks/backlog/runtime/RUNTIME-355-method-execution-retirement-and-consistency.md)
records the explicitly non-blocking simplifications: K-Means' local page transport, unused
test-only queue APIs and owner-step helpers, optional agent argument shape and remaining removal
probes. Initial sampling now uses the shared worker sampler, and runtime CPD with Vulkan subsampling
uses worker steps. GPU Init, Accept and preview collection still need frames;
minimizing the window does not stop worker iterations, but defers owner-side GPU Init and publication.
Only this GPU/driver was exercised; no measured speedup or general numerical proof is claimed.

## Final initial-sampling and CPD worker slice

This bounded addition closes the core review's remaining CPD progress caveat. `PointSamplingGpuRun`
uses the existing `WorkerGpuPages` transport for both CPD subsamples and consolidation's initial
samples. Owner preparation, validation and publication remain on the owner thread; recording,
completion waits and the prefix check run on the worker. CPD's separate pump and the framed
job helper, unused after this change, are removed. No numerical kernel or page budget changed.

The independent plan counter-review (Opus session `c9ca3773-b0db-4131-90a6-cf8220df9290`) returned
**REVISE** for test-budget synchronization and bounded cleanup on assertion failures, while accepting
the design and ownership. Those corrections were implemented. The code review, bound to tree
`cad65177ae05afc77a2c3cb1b4afdee0e847c584`, initially required six verification checks. After those
checks and a small unused-import/comment cleanup, the same reviewer returned **“F3 code, behaviour
and verification: APPROVE.”** for tree `e031b367d8a5529ce69cf4b5d45022bf5b1374a8`.
Its required summary correction below explicitly combines the CPU Ready contract with the GPU
no-frame check; the final reviewer accepted that correction in the overall approval above. Review tools enforce read-only access
and prohibit further delegation. Requested model/effort: Claude Opus 5.5/max; the CLI reported
`claude-opus-5-5` (effort is not independently echoed).

- **CPU:** `IntrinsicRuntimeContractTests` built in `ci`; all 186 cases in
  `ResidentPointSampling`, `PointSamplingOperations`, `CpdBrokerWorkspaceReuse`, `CpdSingleWorkerWait`,
  `CoherentPointDriftOperations`, `ResidentLop`, `PointCloudConsolidationModule`,
  `QueuedEditorJobDriftGuard`, `StepRuns` and `GpuTransactionLifecycle` passed, with a 60-second
  per-case timeout and four CTest slots.
- **Real Vulkan:** `IntrinsicPointLBVHGpuTests`, `IntrinsicRuntimePointCloudConsolidationGpuParityTests`
  and `IntrinsicRuntimeContractTests` built in `ci-vulkan`. All six selected GPU cases passed on
  the RTX 3050 above, with no skips: `ARCH023WorkerProducers.*`, `RUNTIME289VulkanCpdSubsamples.*`,
  `RUNTIME290VulkanPointSampling.*`, and consolidation's `VulkanOrderedInitialSamplesMatchTheCpuOrder`
  and `ResidentAndHybridRunsAdvanceWithGraphicsHeldAndNoFrames`. With the CPU contracts that a ready run submits no ordering page, the new
  subsampled CPD case shows both device orders verified and adopted and at least three iterations
  completed while graphics is held and no owner callbacks or frames run. Backend labels and validation-error assertions passed.
- **Sanitizers:** all five new or rewritten CPU cases passed in `ci-vulkan` under ASan/UBSan:
  the captured CPD sampling contract, held-order cancellation/reuse, batch/step/restart equivalence,
  consolidation order parity/fallback, and consolidation cancel/stale/shutdown.
- **Negative checks:** before the implementation, all five CPU cases and the new real-GPU case
  failed their intended assertions without hanging. The pre-existing GPU producer case stayed
  green. Initial test attempts exposed two fixture mistakes (Bayesian output mode and a mock's
  missing Float64 flag) and stale Vulkan-tree test discovery; these were corrected before counting
  the negative checks. An additional test found a completed target's GPU backend report being lost
  when the source ordering was cancelled; reporting the completed set before returning fixed it.
  Removing CPD's ordering abort, consolidation's cancel wake-up, or adoption of the verified GPU
  order each made its test fail; restoring each mechanism restored green. Source and tests were restored to `cad65177` after those probes; the subsequent
  import/comment cleanup changes no tested mechanism.
- **Structure and reuse:** all 271 source-rule self-checks and the strict layering, feature-boundary,
  task, link, claim, mirror and CPU-budget checks passed. The whole-source duplication scan included
  all 481 module files through byte-identical copies; its only newly matched changed-production
  fragment was a shared import preamble. No semantic duplication model was used. clang-format 20.1.8 checked the changed C++ lines.
  After the import/comment cleanup, the 186 CPU, six GPU and five sanitized cases passed again.

Consolidation has no separate real-GPU no-frame test for its initial-order job; its consumer is
covered by mocks and GPU parity, and CPD exercises the shared worker sampler without frames.
The sampler's residency stamp has no independent removal proof because the pooled workspace's
retained input view masks removal. CPD shutdown relies on device completion, including an ordering
page submitted after the pre-drain idle wait. Initial ordering has no separate Jobs-window row or
stage text; its time is included in step one. No TSan run was performed. These limits are also recorded in the coverage matrix.

The final tooling sweep exposed a separate local root-hygiene failure: an existing
`.pytest_cache/` directory dated 2026-10-07 is not an allowed root entry. It was preserved;
[BUG-259](../../tasks/backlog/bugs/BUG-259-local-pytest-cache-root-hygiene.md) owns that local setup issue.
This does not establish a hosted-CI failure. All 39 tooling regression scripts selected by
`ci-docs.yml`, including all 13 root-hygiene test methods, passed unchanged in an isolated snapshot
of maintained tree `4ef92eb0f0241d28672dfea8afd1bf0b90f41e27`, before this closure narrative.
Strict task policy, state links, documentation links, claims, mirrors
and session-brief checks also passed. The local-cache failure remains open under BUG-259.
The same final reviewer (session `3bed78aa-f0ae-4a1b-bf64-1407f84919d8`) approved the
docs/task closure delta at tree `c48e05aed758d25891f2bc0c10e3130dbfc4bbd5` with verdict **“APPROVE”**
(packet SHA-256 `c904782e22606cffdb1cecbac80697a72ca986d8fd544b9c6b5f08fff03ba6cd`), including the evidence retention boundary and the three applied wording/ordering corrections.

Raw logs, probe outputs and review transcripts are temporary. After acceptance they are removed
under the task-retirement policy; this note preserves the source identity, commands, negative
fixture result and limits, not a replacement raw-evidence archive. No other task's inputs or
worktree is a deletion target.
