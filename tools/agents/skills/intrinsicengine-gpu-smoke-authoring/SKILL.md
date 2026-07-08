---
name: intrinsicengine-gpu-smoke-authoring
description: House pattern for authoring opt-in gpu;vulkan readback smoke tests in IntrinsicEngine — the tests that prove a rendering/runtime seam `Operational` on a Vulkan-capable host. Covers label policy, capability-skip vs hard-fail rules, waiting for IDevice::IsOperational(), backbuffer pixel-sampling idioms (non-black fraction, projected sample points, expected clear color), driving the real UI/command path instead of writing state directly, ci-vulkan build/run incantations, and when a bug fix owes an Operational smoke follow-up. Use when adding or modifying any test labeled gpu/vulkan, proving a fix Operational, writing a readback assertion, or closing a rendering task whose maturity target is Operational.
---

# IntrinsicEngine GPU Smoke Authoring

The repository proved out one test shape for "this actually renders": the
opt-in `gpu;vulkan` readback smoke. It was hand-rolled at least fourteen times
(`BUG-024B`, `BUG-026B`, `BUG-035`, `BUG-060`, the GRAPHICS-032D/033D/038E/
076E/077E/078E/084C/089/090/092 slices) before being codified here. Follow
this pattern instead of re-deriving it.

## When a GPU smoke is owed

- A task's `## Maturity` target is `Operational`: CPU-only contract coverage
  is **insufficient** — the backend-labeled run must be cited in the task's
  `Verification` as actually executed (see `intrinsicengine-task-workflow`).
- A rendering/runtime bug fix closes at `CPUContracted` but the defect was
  only observable on a real backend: name the `Operational` smoke follow-up
  task (the `BUG-024`→`BUG-024B`, `BUG-026`→`BUG-026B` pattern, made policy by
  `HARDEN-077`) or record explicitly that none is owed.
- **Contract-test fidelity warning (`BUG-026`'s post-mortem):** CPU contract
  tests that seed mock readback bytes directly cannot catch convention
  defects — render-id 0 sentinel collisions, UINT clear-value punning, real
  clear colors. If the correctness claim depends on a *convention* between
  CPU and GPU code, only a smoke that exercises the real backend validates it.

## Test shape

- **Location and naming.** Integration-level smokes live next to their peers,
  e.g. `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`;
  new files use `Test.<Name>.cpp`. Reuse the existing bootstrap and readback
  helpers (`BootstrapDefaultSandboxAppEngine` /
  `BootstrapDefaultSandboxAppEngineWithApp`, the readback buffer helpers,
  `ProjectReferenceCameraPixel`) rather than duplicating them.
- **Labels.** `gpu;vulkan` plus category and ownership labels (e.g.
  `integration`, `runtime`, `graphics`). This excludes the test from the
  default CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`) and opts it into
  the Vulkan gate. New labels must update `tests/README.md` **and**
  `tests/CMakeLists.txt` in the same change (AGENTS.md §7).
- **Skip vs fail discipline.** Capability-skip (GTEST_SKIP) is legal only
  *before* the engine runs: no GLFW, no Vulkan device, promoted device cannot
  reach operational. Once `RHI::IDevice::IsOperational()` is true and frames
  have run, assertions **fail** — a post-run skip masks regressions. Wait for
  operational; never gate on Vulkan diagnostics.
- **Bounded frames.** Run a small bounded frame count and account for
  pipeline latency — e.g. `BUG-024B` runs 8 frames to cover the pre-render
  transform flush plus swapchain latency before sampling.
- **Drive the real path.** State changes go through the promoted UI/editor
  command path (e.g. `EditorCommandHistory`), never by writing
  `Transform::WorldMatrix`, GPU instance buffers, or readback bytes directly
  from the test — otherwise the smoke proves nothing about the engine.
- **Readback assertions.** Idioms in order of strength:
  - Full-frame content: `nonBlackPixels > totalPixels / 2` (a lit/cleared
    frame, not a single stray pixel).
  - Expected clear: the default-recipe scene clear is light blue,
    ~RGB(170,203,231) in BGRA8_SRGB — assert it where the background is the
    subject.
  - Point sampling: compute expected pixel locations analytically with the
    reference-camera projection helper (position (0,0,3), fovy 45°, Vulkan
    Y-flip) so the assertion holds for any backbuffer extent; assert the old
    location returned to background *and* the new location contains the
    subject (`BUG-024B`).
  - Where pass structure is the subject, also assert the canonical pass names
    recorded (e.g. `SurfacePass`, `Present`) and that no canonical pass took
    the `SkippedUnavailable` branch.

## Build and run incantations

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target <SmokeTestTarget>
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '<FixtureOrCaseRegex>' -L 'gpu' -L 'vulkan' --timeout 120

# Direct app runs under sanitizers need the leak suppressions and a bound:
LSAN_OPTIONS=suppressions=$PWD/lsan.supp timeout 20s ./build/ci-vulkan/bin/ExtrinsicSandbox
```

The default CPU gate must stay green alongside
(`ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`).

## Retirement discipline

- Cite the smoke run in the task's `Verification` **as actually executed**,
  with host GPU and driver (e.g. "NVIDIA GeForce RTX 3050, driver 590.48.01")
  and pass counts — this is what upgrades maturity to `Operational`.
- Never relax, skip, or delete a readback assertion to make a gate pass; that
  is a forbidden change in every bug task that owns one (`BUG-016`,
  `BUG-024B`). A flaky smoke gets a diagnosis (`intrinsicengine-diagnose`) or
  a labeled quarantine with an owning task, not a weaker assertion.
