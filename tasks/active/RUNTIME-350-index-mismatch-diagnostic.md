---
id: RUNTIME-350
theme: F
depends_on: [RUNTIME-347]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the shared spatial index admission states why a snapshot does not match the captured samples, so the four GPU starts that report its diagnostic (point scalar transaction for kernel density, point spacing and density weights; outlier analysis; keypoints; point normals) refuse with a reason instead of an empty message; CPU paths keep their own wording; statuses are unchanged; no catalog contract covers the admission
---
# RUNTIME-350 — A mismatched index snapshot refuses with a reason on the GPU paths

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
28. (2026-10-29) "Please do 1. and 2." (2: fix the Vulkan path's empty message for a mismatched index
    snapshot now, as its own slice with a GPU regression test)

## Goal
- Found by the `RUNTIME-347` inventory: `AcquirePointIndex`
  (`Runtime.GeometryProcessingOperations.RadiusRows.cpp:18-36`) writes no diagnostic when the
  shared index snapshot does not match the captured samples (`Mismatched`). Every CPU caller words
  its own message ("Density index snapshot does not match the selected samples.", ...), but four GPU
  starts refuse with the empty diagnostic: `StartPointScalarGpu` (`Runtime.PointScalarTransaction.cpp:493`;
  kernel density, point spacing, density weights), outlier analysis (`Outliers.cpp:807`), keypoints
  (`Keypoints.cpp:404`) and point normals (`NormalOperations.Processing.cpp:1142`).

## Plan
- Fix the root cause once, in the shared function: on `Mismatched` it writes "Spatial index
  snapshot does not match the selected samples." The CPU callers do not read the diagnostic on
  `Mismatched` (checked at every call site), so their messages stay.
- Statuses stay: three GPU starts refuse with `InvalidProcessingParameters`, keypoints with
  `GeometryProcessingFailed` (`Keypoints.cpp:364`), while the CPU paths report the same condition as
  `StaleEntity`. Aligning them touches four call sites and is a behaviour change of
  its own; stated, not done here.

## Tests (predictions as assertions)
- `PointIndexAdmission.MismatchedSnapshotExplainsItself` (contract, CPU): an admission whose slots and
  points differ from the indexed scene answers `Mismatched` with that diagnostic. Red before the fix
  (empty diagnostic).
- `RUNTIME350IndexMismatch.GpuStartExplainsAMismatchedSnapshot` (Vulkan smoke): a kernel density GPU
  start whose captured scene differs from the indexed one refuses (null handle,
  `InvalidProcessingParameters`) with that message. Red before the fix (empty message).
- Refactoring pin: the CPU mismatch messages (`KernelDensityOperations.IndexAdmissionRejectsMismatchedPositionsAndSourceRows`
  and the other operations' suites) and the GPU smokes pass unchanged.

## Acceptance criteria
- [x] The shared admission explains a mismatch; both regression tests red before, green after.
- [x] Affected suites green; Codex code review; structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -j8 -R "$(cat <regex of RUNTIME-347>)|^PointIndexAdmission\."
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 600 -R "<every test of IntrinsicPointLBVHGpuTests>"   # 57
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-29: Plan from the `RUNTIME-347` inventory finding; all `AcquirePointIndex` call sites read.
- 2026-10-29: Both regression tests written first and red on the unchanged code:
  `PointIndexAdmission.MismatchedSnapshotExplainsItself` (diagnostic ""), and
  `RUNTIME350IndexMismatch.GpuStartExplainsAMismatchedSnapshot` on the Vulkan device (status
  `InvalidProcessingParameters` as predicted, message ""). Fix: `AcquirePointIndex` writes the
  diagnostic on `Mismatched`, contract comment updated. After it: both green; the `RUNTIME-347`
  list plus the new test 791/791; every test of `IntrinsicPointLBVHGpuTests` (57, including the
  outlier, keypoint and normal transactions) passes, one skipped by design
  (`METHOD056VulkanCpdEStep.ScalingProfile`, a benchmark profile). Changed lines clang-format 20.1.8.
- 2026-10-29: Codex code review (`review350.answer.md`): approve; all twelve call sites checked
  (eight return their own wording, the four GPU starts pass the diagnostic on), both tests pin the
  defect. O1 taken: keypoints' refusal status is `GeometryProcessingFailed`, now stated.
