---
id: GRAPHICS-137
theme: B
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Backend-internal Vulkan realization spike behind the unchanged RHI surface.
  No geometry element-domain, property, parameterization, support-radius, or
  method-integration surface is touched. If the spike is forced to change an
  RHI interface, that boundary leak is the experiment's finding and is
  handled through ADR-0028 and GRAPHICS-136, not silently in this task.
---
# GRAPHICS-137 — Shader-object realization spike (ADR-0028 killing experiment)

## Goal

- Implement a second realization of the existing declarative RHI state
  description inside `src/graphics/vulkan/` using `VK_EXT_shader_object` +
  dynamic state, behind the *unchanged* `RHI::PipelineManager` / `IDevice`
  API, and record whether the boundary holds (no caller changes needed) or
  leaks (callers must change) — the ADR-0028 Decision 4 killing experiment.

**Gate — do not start before the trigger.** This spike executes only after an
ADR-0028 Decision 3 trigger has fired: measured pipeline-compile stalls in a
real workload, or a landing material/permutation slice that multiplies state
variants. Building a second realization with no consumer pressure is exactly
the speculative machinery ADR-0028 forbids.

## Non-goals

- No `GraphicsStateResolver`, no desc-keyed cache, no fallback-while-compiling
  machinery, no `VK_EXT_descriptor_heap` adoption (each has its own ADR-0028
  trigger).
- No RHI surface rename — that is `GRAPHICS-136`, gated on this task's
  outcome.
- No switch of the default Vulkan realization path; the spike stays behind an
  explicit opt-in (config or build flag) and may be discarded after the
  finding is recorded.
- No performance claim without the benchmark protocol (`AGENTS.md` §8);
  the spike's deliverable is a boundary finding, not a perf win.

## Context

- Owning layer: `src/graphics/vulkan/` (backend-internal); the RHI surface
  (`RHI.PipelineManager`, `RHI.Descriptors`) must remain unchanged — any
  needed change there is the leak finding.
- ADR-0028 records the invariant this spike tests and the falsifiable
  prediction: "an alternate realization can be implemented behind the
  unchanged `PipelineManager` API without touching any caller."
- Device capability handling follows the operational-readiness discipline:
  hosts/drivers without `VK_EXT_shader_object` keep the `VkPipeline`
  realization; gating is by device capability, never by Vulkan diagnostics
  (`RHI::IDevice::IsOperational()` discipline, ADR-0004/0005).
- Fired trigger (fill before starting): _pending — name the measured stall or
  the landed permutation slice here._

## Required changes

- [ ] Record the fired trigger in `## Context` (prerequisite; the task stays
      blocked without it).
- [ ] Add an opt-in shader-object realization path inside
      `src/graphics/vulkan/` that consumes the existing `PipelineDesc`
      (shaders → `VkShaderEXT`, raster/depth/blend → dynamic state commands,
      attachment formats → dynamic rendering) with capability-gated fallback
      to the `VkPipeline` path.
- [ ] Keep `RHI::PipelineManager` and `RHI::IDevice` signatures unchanged; if
      a change proves unavoidable, stop, record the exact leak (which field,
      which call site, why) in this task and ADR-0028, and end the spike with
      that finding.
- [ ] Record the experiment outcome (holds/leaks + evidence) in `## Context`
      of `GRAPHICS-136` and in ADR-0028's Validation section.

## Tests

- [ ] Opt-in `gpu;vulkan` readback parity smoke: the reference scene renders
      pixel-identical (or within the documented tolerance) under the
      `VkPipeline` realization and the shader-object realization on a
      capable host.
- [ ] Default CPU gate stays green (spike is backend-internal and opt-in).
- [ ] Capability-skip behavior covered: on hosts without
      `VK_EXT_shader_object`, the opt-in path reports unsupported and falls
      back without diagnostics-based gating.

## Docs

- [ ] ADR-0028 Validation section updated with the recorded outcome.
- [ ] `docs/architecture/graphics.md` updated only if the spike is kept as a
      selectable realization; a discarded spike leaves no doc trace beyond
      the ADR/task record.

## Acceptance criteria

- [ ] Fired trigger recorded before any code change.
- [ ] Boundary finding (holds/leaks) recorded in this task, `GRAPHICS-136`,
      and ADR-0028, with the parity-smoke evidence cited.
- [ ] RHI surface diff is empty, or the leak is documented as the finding and
      no silent surface change ships.
- [ ] Default CPU gate and layering gate green; `gpu;vulkan` smoke cited from
      an actual run on a capable host.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
git diff --stat -- src/graphics/rhi/   # expect: empty, or the documented leak finding
```

On a Vulkan-capable host with `VK_EXT_shader_object` support:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- Starting before an ADR-0028 trigger has fired and is recorded in
  `## Context`.
- Changing the RHI surface silently instead of recording the leak finding.
- Switching the default realization path or removing the `VkPipeline` path.
- Adding resolver/cache/heap machinery beyond the spike's scope.
- Claiming a performance result without the §8 benchmark protocol.

## Maturity

- Target: `Operational` on `VK_EXT_shader_object`-capable Vulkan hosts
  (parity smoke actually run); `CPUContracted` is not a meaningful stop-state
  for a backend-internal realization spike — if no capable host is available,
  the task stays open rather than closing on CPU-only evidence.
