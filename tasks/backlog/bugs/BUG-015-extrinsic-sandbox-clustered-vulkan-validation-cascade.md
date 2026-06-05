# BUG-015 — ExtrinsicSandbox clustered Vulkan validation cascade

## Status

- Status: open.
- Reported: 2026-06-05 from a local debug-build
  `ExtrinsicSandbox` run.
- Owner/layer: `graphics/vulkan`, `graphics/rhi`, `graphics/renderer`, and
  runtime-owned promoted Vulkan operational gating. `app` remains out of scope.

## Goal
- Fix the promoted Vulkan default sandbox path so `ExtrinsicSandbox` reaches an
  operational default-recipe frame with validation layers enabled after the
  clustered-lighting path is present, without descriptor-layout, queue-ownership,
  command-buffer-lifetime, or transient-image usage validation errors.

## Non-goals
- Do not disable validation layers or demote the sandbox to the Null backend to
  hide the defect.
- Do not skip `ClusterGridBuildPass` or `LightClusterAssignmentPass` as a
  workaround unless the task records a separate follow-up that restores the
  clustered-lighting operational path.
- Do not introduce app-layer Vulkan knowledge; fixes must stay in renderer/RHI/
  Vulkan/runtime ownership.
- Do not treat the local `cmake-build-debug` tree as final verification evidence;
  it is only the reported repro trigger.

## Context
- Symptom: running
  `/home/alex/Documents/IntrinsicEngine/cmake-build-debug/bin/ExtrinsicSandbox`
  reports promoted Vulkan selection, then remains fail-closed with
  `VulkanRequestedButNotOperational status=RequestedButIncompleteGate
  reason=BarrierValidationFailed`.
- Expected behavior: the promoted Vulkan device should either reject a malformed
  default recipe before command submission with a precise diagnostic, or record a
  validation-clean default-recipe frame and publish operational status.
- Impact: the app-default Vulkan path regresses after the clustered-lighting
  task series. Even after the ImGui black-window regression was fixed in
  `BUG-014`, the sandbox cannot be considered operational while validation
  reports descriptor-layout and synchronization errors on startup.
- First validation class to fix: compute clustered-lighting pipeline layouts do
  not match shader resources. Validation reports
  `VUID-VkComputePipelineCreateInfo-layout-07990` because `uClusterGrid` at
  `set = 0, binding = 0` is a shader storage buffer while the pipeline layout
  provides `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, and
  `VUID-VkComputePipelineCreateInfo-layout-07988` because `uLights`,
  `uHeaders`, `uIndices`, and `uCounter` storage-buffer bindings are missing
  from the pipeline layout.
- Secondary validation classes captured in the same run must be verified after
  the pipeline-layout fix because they may be cascades:
  duplicate queue-family ownership barriers for `GpuWorld.Lights`,
  `ClusterLights.Headers`, and `ClusterLights.Indices`; unmatched
  compute-to-graphics acquire barriers for cluster-light buffers; command-pool
  reset while a command buffer is pending; transient resource allocation failure;
  depth layout transitions against color-only images; `SceneDepth` transitioning
  to a color-attachment layout; and sampled descriptor writes against images
  missing sampled usage.
- Relevant shader resources:
  `assets/shaders/cluster_grid_build.comp` declares `uClusterGrid` at
  `set = 0, binding = 0` as `std430` storage buffer, while
  `assets/shaders/light_cluster_assign.comp` declares `uClusterGrid`,
  `uLights`, `uHeaders`, `uIndices`, and `uCounter` as storage buffers at
  bindings 0..4.
- Relevant recent tasks: `GRAPHICS-039A` added cluster-grid build,
  `GRAPHICS-039B` added light-cluster assignment, `GRAPHICS-039D` tagged the
  cluster passes for async compute, and `BUG-014` restored visible ImGui output
  but did not cover the clustered-lighting validation-clean startup path.

## Required changes
- [ ] Build a deterministic feedback loop with validation layers enabled that
      reproduces the reported sandbox failure on a Vulkan-capable host, using
      the local debug command only as an initial clue and a preset-based
      `ci-vulkan` target for authoritative verification.
- [ ] Fix clustered compute pipeline layout creation so the cluster-grid and
      light-assignment compute shaders receive storage-buffer descriptors for
      all declared resources instead of the bindless sampled-image layout.
- [ ] Add CPU-visible descriptor-layout/reflection contract coverage that would
      fail when a compute shader resource declaration is backed by the wrong
      descriptor type or missing from the `VkPipelineLayout`.
- [ ] Audit async-compute queue-ownership barrier generation for clustered
      lighting and remove duplicate ownership transfers while preserving
      required release/acquire pairing between graphics and compute queues.
- [ ] Add contract or Vulkan regression coverage for matching release/acquire
      ownership barriers on `GpuWorld.Lights`, `ClusterLights.Headers`, and
      `ClusterLights.Indices`.
- [ ] Audit transient image/resource allocation and render-pass attachment
      routing so depth attachments always use depth-capable images, sampled
      descriptors only bind sampled-capable images, and `SceneDepth` is never
      transitioned to a color-attachment layout.
- [ ] Confirm whether the command-pool reset warning is a cascade from earlier
      invalid submits or a separate lifecycle bug; fix it or split a follow-up
      with a precise reproducer.
- [ ] Keep runtime operational gating fail-closed until the final validation
      clean frame is published; improve diagnostics if the first failing gate is
      currently obscured by later cascades.

## Tests
- [ ] Add or update a `gpu;vulkan` regression that runs the app-default or
      default-recipe promoted Vulkan path with validation layers enabled and
      fails on any validation error from the captured classes.
- [ ] Add focused `contract;graphics` coverage for clustered compute pipeline
      descriptor layout requirements, including `uClusterGrid`, `uLights`,
      `uHeaders`, `uIndices`, and `uCounter`.
- [ ] Add focused barrier/queue-affinity coverage for clustered async-compute
      release/acquire pairing and duplicate ownership-transfer suppression.
- [ ] Add resource-usage/layout coverage proving color, depth, and sampled
      transient images cannot be cross-bound through stale framegraph handles or
      incomplete usage flags.
- [ ] Preserve the default CPU-supported gate and existing ImGui/default-recipe
      Vulkan smokes.

## Docs
- [ ] Update `src/graphics/vulkan/README.md` or the relevant renderer README
      with the clustered compute descriptor-layout contract if a new backend
      layout policy is introduced.
- [ ] Update `docs/architecture/rendering-three-pass.md` if the clustered
      lighting pass resource bindings, queue ownership, or transient-resource
      ownership model changes.
- [ ] Update this bug task and `tasks/backlog/bugs/index.md` when the root cause
      and verified fix are known.

## Acceptance criteria
- [ ] The first validation error from the reported log,
      `VUID-VkComputePipelineCreateInfo-layout-07990` for `uClusterGrid`, no
      longer appears under the preset-based Vulkan repro.
- [ ] The missing storage-buffer binding validation errors for `uLights`,
      `uHeaders`, `uIndices`, and `uCounter` no longer appear.
- [ ] Queue ownership barriers for clustered-lighting buffers are paired,
      non-duplicated, and validation-clean.
- [ ] No command-pool reset occurs while command buffers are pending.
- [ ] No default-recipe transient image is used with an incompatible layout,
      attachment role, descriptor type, or usage flag.
- [ ] `ExtrinsicSandbox` reaches an operational promoted Vulkan frame on a
      Vulkan-capable host with validation enabled.
- [ ] The fixing commit cites the hypothesis that proved correct and the
      validation-layer evidence that verifies the fix.

## Verification
```bash
# Initial repro signal from the report; not sufficient final evidence.
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    /home/alex/Documents/IntrinsicEngine/cmake-build-debug/bin/ExtrinsicSandbox

# Authoritative Vulkan setup.
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests

# Validation-layer sandbox/default-recipe proof. Use a bounded timeout because
# ExtrinsicSandbox is interactive.
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    timeout 10s ./build/ci-vulkan/bin/ExtrinsicSandbox

# Focused Vulkan regression once added or updated.
ctest --test-dir build/ci-vulkan --output-on-failure \
    -R 'ExtrinsicSandbox|DefaultRecipeSurfaceGpuSmoke|RuntimeSandboxAcceptanceGpuSmoke' \
    --timeout 120

# Full opt-in GPU/Vulkan smoke gate on a Vulkan-capable host.
ctest --test-dir build/ci-vulkan --output-on-failure \
    -L 'gpu' -L 'vulkan' -LE 'slow|flaky-quarantine' --timeout 120

# CPU/null guardrail.
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
    -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Structural checks.
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Shipping a fix without a regression test when a Vulkan-capable host can
  reproduce the validation errors.
- Silencing validation messages without fixing the underlying descriptor,
  synchronization, command-buffer, or image-usage bug.
- Moving Vulkan descriptor, queue-family, or image-layout policy into
  `runtime`, `app`, `ecs`, `assets`, or `platform`.
- Reverting unrelated graphics/runtime changes from the ten-task backlog loop.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; the default CPU/null contract
  gate must remain green.
