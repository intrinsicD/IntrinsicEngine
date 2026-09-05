---
id: BUG-167
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "interactive CI-routing repair; evidence is the reviewed workflow diff and hosted ci-vulkan result"
owner: Codex
branch: codex/bug-167-vulkan-lop-display-routing
worktree: /tmp/intrinsic-bug167
claimed_at: "2026-09-05T02:20:00+02:00"
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog; this repair changes only which existing gpu;vulkan CTest cases run inside the hosted virtual-display lane. It changes no engine, benchmark schema, claim, geometry, method, backend-selection, or reusable workflow-evidence contract."
---
# BUG-167 — Hosted Vulkan omits LOP benchmark runtime prerequisites

## Goal
- Keep the positive LOP-family Vulkan benchmark and its validator out of the
  displayless capability batch and execute them in the existing Xvfb/lavapipe
  operational batch, while retaining the negative unavailable-environment
  proof in the displayless batch. Ensure that clean hosted configuration can
  compile the runtime SPIR-V required by that positive path.

## Acceptance criteria
- [x] `IntrinsicLopFamilyGpuBenchmarkSmoke.Run` and `.Validate` execute under
      the hosted virtual display; the unavailable-environment regression stays
      outside that selection and continues to pass.
- [x] Workflow regressions pin the five-case operational selection and prevent
      the positive benchmark pair from drifting back into the displayless batch.
- [x] The hosted package set installs and probes `glslc`, so a fresh configure
      wires each `intrinsic_add_glsl_shaders(...)` dependency instead of
      silently omitting every SPIR-V build edge.
- [ ] Hosted `ci-vulkan` passes with the positive benchmark result validated;
      no GPU/Vulkan assertion, benchmark validator, or fail-closed behavior is
      weakened.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tools/ci/check_workflow_names.py --root .github/workflows
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGpuVulkanTests
LIBGL_ALWAYS_SOFTWARE=1 \
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
xvfb-run -a --server-args="-screen 0 1280x720x24" \
  ctest --test-dir build/ci-vulkan --output-on-failure \
    -R '^(IntrinsicLopFamilyGpuBenchmarkSmoke\.(Run|Validate))$' \
    -L gpu -L vulkan --no-tests=error --timeout 180
# Hosted evidence: ci-vulkan concludes green.
```

## Context
- PR #1037 run `33931586905` built the complete Vulkan target set, then the
  displayless batch passed or capability-skipped 50 of 51 cases before
  `IntrinsicLopFamilyGpuBenchmarkSmoke.Run` failed closed with GLFW diagnostic
  `X11: The DISPLAY environment variable is missing`; its fixture-dependent
  validator consequently did not run.
- The workflow already provisions Xvfb and lavapipe for three non-skipped
  operational cases, but its exclusion/selection regex predates the LOP-family
  benchmark registration. This is routing drift, not a BUG-166 source defect.
- The revised regex enumerates exactly five configured cases: the two Sandbox
  contracts, runtime readback smoke, and LOP-family `.Run`/`.Validate` pair.
  The 20-case workflow regression suite and workflow-name check pass locally.
- PR #1038 run `33933001077` then proved the routing repair: all five cases
  entered the Xvfb batch and the three pre-existing operational cases passed.
  The LOP runner failed because hosted configure explicitly reported `glslc
  not found`, omitted every shader build dependency, and left the promoted
  device non-operational with missing `shaders/*.spv`; `.Validate` correctly
  did not run after its fixture failed. Ubuntu 24.04 provides the `glslc`
  package, so the workflow now installs and probes that existing required
  compiler rather than changing any engine or benchmark failure contract.
- This host does not provide `xvfb-run`, but the same lavapipe-selected pair
  passed 2/2 under its existing `DISPLAY=:1`; the hosted lane owns the final
  clean-checkout Xvfb proof.
