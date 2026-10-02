---
id: GRAPHICS-159
theme: F
depends_on: [UI-073]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: GPU evidence follow-up; evidence is the gpu;vulkan smoke output.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. This only extends an existing smoke to prove the UI-073 run-job lifecycle on a device; it changes no property, publication, recipe-slot or locality contract.
---
# GRAPHICS-159 — Texture bake run job and device-loss evidence on Vulkan

## Goal
Prove on a Vulkan host that a texture bake's run job (UI-073) ends Succeeded and
its record Published once the bake reaches Ready. Prove also that
`IDevice::IsDeviceLost` alone fails in-flight bakes, while a device that is merely
not operational does not.

## Context
- UI-073 (2026-10-02) made each bake submit one run job that parks until the
  shared `BakeRun` settles.
- It added `IDevice::IsDeviceLost` so that only a real device loss withdraws bakes.
- CPU and mock tests cover every path except Ready→Published and real device
  loss, which need a GPU frame.
- Extend `tests/integration/graphics/Test.PropertyTextureBakeGpuSmoke.cpp`.
- Run it under Xephyr (see the memory notes). Run the binary directly and check
  for `[       OK ]`.

## Acceptance criteria
- [ ] The smoke asserts that the bake's run job reaches JobState Succeeded, the record is Published and the texture is Ready. A cancelled bake ends Cancelled and leaves the texture unchanged.
- [ ] Device loss is either injected through a supported test seam or recorded as not injectable. A swapchain-not-ready or non-operational frame does not fail an in-flight bake.

## Verification
```bash
cmake --build build/ci-vulkan -j$(nproc)
# under Xephyr; run the binary directly and check for [       OK ]
ctest --test-dir build/ci-vulkan -R 'PropertyTextureBakeGpuSmoke' -L gpu -L vulkan --output-on-failure --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```
