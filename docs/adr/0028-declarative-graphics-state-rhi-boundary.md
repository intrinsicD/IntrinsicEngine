# ADR 0028: Declarative graphics state at the RHI boundary

- **Status:** Accepted
- **Date:** 2026-08-24
- **Owners:** Rendering / RHI
- **Related tasks:** GRAPHICS-136, GRAPHICS-137

## Context

The RHI boundary already describes GPU executable state declaratively rather
than as API pipeline objects:

- `RHI::PipelineDesc` (`src/graphics/rhi/RHI.Descriptors.cppm`) carries shader
  paths, raster/depth-stencil/blend state, topology, push-constant size, and
  dynamic-rendering attachment formats. Vertex input state is intentionally
  absent (BDA-only geometry), and no `Vk*` type crosses the boundary
  (`AGENTS.md` §2 layering table).
- Two realizations of the same description exist today: the promoted Vulkan
  device compiles it to `VkPipeline` objects with dynamic rendering; the Null
  device realizes it as nothing while keeping the CPU/null gate meaningful.
- `RHI::PipelineManager` is an async-capable pool (compile callback,
  `IsReady`, `Recompile`) with lease-based lifetime.

The API landscape has made "pipeline" an explicitly backend-specific packaging
choice (external state verified 2026-08-24):

- Vulkan's Roadmap 2026 direction pairs `VK_EXT_shader_object` (independently
  bound stages + dynamic state) with `VK_EXT_descriptor_heap`
  (descriptor-set-free binding; EXT in the feedback stage, SDK 1.4.341+, KHR
  promotion pending). The Khronos SIGGRAPH 2026 course "How to write a Vulkan
  application in 2026" teaches shader objects + dynamic rendering + descriptor
  heaps as the modern entry path
  (<https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension>,
  <https://docs.vulkan.org/tutorial/latest/courses/siggraph2026_vk_tutorial/00_Overview.html>).
- D3D12 deliberately retains monolithic PSOs; Metal 4 sits between with
  flexible/specializable pipelines.

The same declarative description therefore maps to *different* native
packagings per backend, and which desc fields participate in a compiled-object
cache key is backend knowledge. Today the engine has no pipeline-permutation
pressure: the pass set is recipe-driven and fixed, states are created at
initialization, and one GPU backend (Vulkan) exists beside Null.

## Decision

1. **The RHI boundary stays a declarative graphics-state description.** A
   graphics (or compute) pipeline is a backend realization strategy, not an
   engine-level architectural concept. Code above the backend must not depend
   on one description mapping to one native pipeline object, must not compute
   backend cache keys, and must not import shader-object/pipeline vocabulary
   from a specific API generation.
2. **No resolver machinery now.** No `GraphicsStateResolver` layer, no
   multi-backend key normalization, and no desc-keyed runtime cache are built
   while no consumer exists (right-sizing: a framework with zero consumers).
3. **Trigger-gated adoptions.** Each deferred adoption has a named
   reintroduction trigger:
   - *Shader-object realization inside `graphics/vulkan`* — trigger: measured
     pipeline-compile stalls, or a first material/permutation slice that
     multiplies state variants; owned by `GRAPHICS-137`.
   - *`VK_EXT_descriptor_heap` adoption inside the backend* — trigger: KHR
     promotion + target-driver coverage + a measured binding bottleneck the
     current bindless (descriptor-indexing) path cannot absorb.
   - *Desc-keyed dedup cache + fallback-while-compiling* — trigger: the first
     runtime-generated (not init-time) state requests.
   - *Mechanical rename `Pipeline*` → `GraphicsState*` at the RHI surface* —
     trigger: a second realization strategy actually landing; owned by
     `GRAPHICS-136`.
4. **Killing experiment.** The falsifiable prediction behind this decision:
   an alternate realization (e.g. shader objects) can be implemented behind
   the *unchanged* `PipelineManager` API without touching any caller. If the
   spike shows callers must change, the boundary leaks, and that finding —
   not API fashion — justifies the reshape. The spike is owned by
   `GRAPHICS-137`; its outcome is recorded there and in `GRAPHICS-136`
   before the rename executes.

## Consequences

- Positive: no rename/refactor churn now; backend-internal freedom to adopt
  shader objects or descriptor heaps later without renderer changes; the rule
  prevents pipeline semantics from re-leaking upward in future slices.
- Trade-off: the `Pipeline*` names at the RHI surface temporarily overspecify
  ("pipeline" suggests one native object); accepted until the trigger fires.
- Risk: resolver machinery gets built speculatively; this ADR forbids that
  until a named trigger fires.
- Follow-up: `GRAPHICS-136` (gated mechanical rename).

## Alternatives Considered

- **Adopt the full resolver proposal now** (`GraphicsStateResolver`, backend
  key normalization, multi-backend cache taxonomy): rejected — one GPU
  backend plus Null exists, D3D12/Metal/WebGPU have no task, and the
  permutation problem the resolver solves does not exist here yet.
- **Rename immediately without machinery:** rejected — 30+ files of
  mechanical churn with no behavior change and no second realization to make
  the new name true; the recorded invariant delivers the value at zero churn.
- **Do nothing / leave undocumented:** rejected — without the recorded rule
  and triggers, future slices would re-derive or erode the boundary.

## Validation

- `python3 tools/repo/check_layering.py --root src --strict` keeps `Vk*`
  types out of RHI/renderer surfaces.
- Existing contract tests pin desc semantics (e.g.
  `SelectionOutlinePipelineSurvivesOperationalRebuild`).
- The killing experiment in Decision 4 validates or falsifies the boundary
  when its trigger fires; the rename in `GRAPHICS-136` may execute only after
  that record exists.
