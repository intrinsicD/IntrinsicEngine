---
id: GRAPHICS-123
theme: B
depends_on:
  - REVIEW-003
  - LEGACY-043
maturity_target: Operational
---
# GRAPHICS-123 — Slang single-kernel gradient pilot

## Goal

- Prove one deterministic offline Slang-to-SPIR-V-and-reflection pipeline and
  one pure differentiable compute kernel whose value and gradient agree with
  independent analytic C++ and finite-difference oracles on Vulkan.

## Non-goals

- No canonical shader-language migration, GLSL deletion, broad shader-module
  taxonomy, material generics, or shader-source conversion.
- No runtime Slang linkage/compiler, file watcher, hot reload, pipeline cache
  redesign, or reflection-driven material framework.
- No differentiable frame graph, loss pass, adjoint lifetime system,
  `GradientSnapshot`, optimizer, inverse-rendering method, or neural model.
- No new RHI autodiff interface or backend registry; Vulkan consumes ordinary
  SPIR-V through the existing pipeline path.
- No claim that one pilot kernel makes archived `GRAPHICS-041` or
  `GRAPHICS-051` operational.

## Context

- Owner/layer: build tooling for offline compilation, tracked shader assets,
  existing `graphics/rhi` SPIR-V loading, and backend-local Vulkan execution.
- Retired `BUILD-001`/`BUILD-002` provide the current GLSL-to-SPIR-V build
  wiring; retired `GRAPHICS-023` provides last-known-good reload contracts.
  This task adds one parallel offline proof without disturbing either path.
- Archived `GRAPHICS-041` and `GRAPHICS-051` are planning parents whose broad
  implementation children remain unopened. This task intentionally proves
  only compiler artifacts, minimal reflection, Slang AD, and Vulkan numeric
  execution.
- `LEGACY-043` removes stale shaders incompatible with the promoted single-set
  layout before the pilot establishes a new authored source.
- Sources: [Slang repository](https://github.com/shader-slang/slang),
  [Slang automatic-differentiation guide](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/07-autodiff.html),
  and [Graphics Programming Weekly Issue 445](https://www.jendrikillner.com/post/graphics-programming-weekly-issue-445/).
- Dependency policy: use the `shader-slang` vcpkg port pinned through the
  repository manifest/override/overlay path. This supersedes archived
  `GRAPHICS-041` FetchContent/`external/cache` language.

## Right-sizing

- Element under evaluation: one offline compiler invocation and one pure
  compute kernel, not a shader platform.
- Simpler alternative: extend the existing shader-build helper with a single
  explicit Slang source/entry declaration and emit ordinary `.spv` plus one
  small deterministic reflection artifact.
- Blast radius: vcpkg metadata, one build helper path, one `.slang` source,
  one CPU oracle test, one Vulkan smoke, and narrow docs.
- Reintroduction trigger: broader modules, hot reload, or frame-graph AD may be
  proposed only after a second real consumer cites this operational proof.

## Required changes

- [ ] Pin `shader-slang` through `vcpkg.json` and, if needed, a repository
      overlay/override. Do not use FetchContent, ad-hoc compiler binaries,
      network downloads, or runtime compiler linkage.
- [ ] Add one explicitly listed pure compute kernel with no texture sampling,
      atomics, global mutable state, or unbounded control flow. Its scalar loss
      must have a hand-derived analytic gradient over a compact input/parameter
      record.
- [ ] Compile the primal and Slang-generated derivative entry points offline
      to SPIR-V and emit a deterministic reflection sidecar containing only the
      bindings, entry points, push constants, and specialization constants the
      pilot actually consumes.
- [ ] Keep generated artifacts in the established build shader tree; if
      dependency-free checked-in artifacts are required, add a regenerate-and-
      compare staleness check rather than a runtime compiler fallback.
- [ ] Add an independent double-precision C++ evaluator and central finite-
      difference oracle. Neither oracle may share generated Slang code or
      derivative implementation logic.
- [ ] Execute primal and derivative SPIR-V through the existing Vulkan compute
      pipeline/readback path and compare values/gradients across ordinary,
      zero, large-but-finite, and near-sensitive inputs with documented
      absolute/relative tolerances.
- [ ] Fail closed with structured build/test diagnostics when Slang is absent,
      reflection is stale/malformed, compilation fails, the device is not
      operational, or numeric output is non-finite. GPU absence is a capability
      skip only in the opt-in GPU test.

## Tests

- [ ] CPU unit tests compare analytic and finite-difference gradients and pin
      tolerance sensitivity independently of Slang/Vulkan.
- [ ] Build/contract tests validate deterministic compiler invocation,
      expected generated artifacts, reflection parse/shape, and stale-artifact
      detection.
- [ ] An opt-in `gpu;vulkan` smoke executes both entry points, reads results
      back, proves primal/gradient parity, and verifies the requested pipeline
      was operational rather than silently skipped or substituted.
- [ ] Existing GLSL shader compilation, direct-SPIR-V loading, and default CPU
      tests remain unchanged and green.

## Docs

- [ ] Document the single-kernel build invocation, pinned compiler version,
      generated/reflection artifact locations, regeneration rule, supported AD
      subset, tolerances, and known Slang side-effect/resource limitations.
- [ ] State explicitly that GLSL remains canonical outside this pilot and that
      archived `GRAPHICS-041`/`GRAPHICS-051` are not implemented by it.

## Acceptance criteria

- [ ] A clean configured build deterministically produces the pilot SPIR-V and
      reflection artifact through the vcpkg-pinned compiler path.
- [ ] Independent analytic, finite-difference, and Vulkan Slang gradients agree
      within documented tolerances on the complete fixture set.
- [ ] An actually run `gpu;vulkan` smoke proves the Vulkan path operational.
- [ ] No runtime compiler dependency, general shader framework, hot reload,
      differentiable frame graph, or production shader migration lands.
- [ ] Default CPU and structural gates remain green.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Slang|Gradient' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'Slang.*Gradient' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- FetchContent, downloaded compiler binaries, runtime Slang linkage, or any
  dependency path outside vcpkg manifest/override/overlay files.
- Broad shader migration, file watching, hot reload, material specialization,
  or reflection framework work.
- Reusing generated derivative logic in either independent oracle.
- Claiming `GRAPHICS-041` or `GRAPHICS-051` maturity from the pilot.
- Passing Slang or Vulkan implementation types through public RHI/renderer
  APIs.

## Maturity

- Target: `Operational` on a Vulkan-capable host, with `CPUContracted` analytic,
  finite-difference, build-artifact, and reflection evidence everywhere else.
