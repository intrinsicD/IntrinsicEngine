# Unified GPU method execution: verification and review

Scope: [ARCH-023](../../tasks/active/ARCH-023-unified-gpu-method-runs.md), the existing GPU/hybrid
methods and their CPU counterparts. This is an engineering refactor and deterministic contract
verification, not a new scientific result, a performance measurement or a cross-device guarantee.
The [coverage matrix](../methods/gpu-method-coverage.md) owns the current method inventory and
limitations; the [method template](../methods/method-template.md) owns the reusable pattern.

## Source and verification

The combined CPU/Vulkan run kept production source unchanged at Git tree
`ee4ed57b0cd4c9951d31993427ade413889cb63d`. The final scalar fixture correction and CTest
registration are in tree `cf1fa7f59b9842e389b70cbb5a11eb8f08df8c1c`; no runtime, kernel,
plugin or method source changed between them. The implementation commit is identified by its
`ARCH-023` commit-message task ID. Test counts below are verification results, not benchmarks.

- **CPU:** `cmake --build --preset ci --target IntrinsicTests`, then
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j4`:
  4,500 selected, zero failures; numeric-locale and GLFW/LSAN environment checks skipped.
  `SandboxEditorPresentation.SourceRules` also passed separately.
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
family consumers against those reviews. Final acceptance is pending the final evidence review.
The reviewer decides the task's checkboxes; source approval alone is not backend verification.

The architecture scorecard found layer imports, CMake links, downward-facing APIs, renderer
ownership and exception ownership sound. Typed pass IDs and new recipe edges were not applicable.
Maturity closure awaits the final acceptance verdict. There are no new layer-policy exceptions.

[RUNTIME-355](../../tasks/backlog/runtime/RUNTIME-355-method-execution-retirement-and-consistency.md)
records the explicitly non-blocking simplifications: K-Means' local page transport, unused
test-only queue APIs and owner-step helpers, framed initial sampling and CPD subsampling’s whole-run owner pump, optional agent argument
shape and remaining removal probes. GPU Accept and preview collection still need frames;
minimizing the window does not stop worker iterations, but defers owner-side GPU publication.
Only this GPU/driver was exercised; no measured speedup or general numerical proof is claimed.

Raw logs, probe outputs and review transcripts are temporary. After acceptance they are removed
under the task-retirement policy; this note preserves the source identity, commands, negative
fixture result and limits, not a replacement raw-evidence archive. No other task's inputs or
worktree is a deletion target.
