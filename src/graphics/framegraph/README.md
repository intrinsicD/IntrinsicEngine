# Graphics Framegraph

`src/graphics/framegraph` owns backend-agnostic render-graph resource declarations,
pass declarations, compilation, validation, transient allocation, barrier packet
inference, and execution ordering. It does not own renderer recipe selection,
backend command bodies, platform windows, swapchains, runtime extraction, live ECS
state, or asset-service traffic.

## Ownership boundaries

- `graphics/renderer` owns `FrameRecipe` identities, recipe feature gates, pass
  labels, renderer diagnostics, and pass-body command contracts.
- `graphics/framegraph` consumes recipe-declared resources/passes, validates
  imported-resource write authorization, computes first/last resource uses, and
  emits barrier packets in pass order.
- `graphics/vulkan` lowers compiled graph resources/barriers/commands to native
  Vulkan objects and owns swapchain/acquire/present mechanics.
- `runtime` submits immutable render snapshots; graphics never queries live ECS
  ownership.

## Default Recipe Contract

The canonical renderer recipe is built by
`BuildDefaultFrameRecipe(graph, features, imports, sizing, shadowSizing)` in
`Extrinsic.Graphics.FrameRecipe` and declared by
`DescribeDefaultFrameRecipe(features)`. The framegraph treats those declarations
as the single source of truth for imported-resource write authorization,
transient-resource lifetime, pass ordering, and final backbuffer presentation.

The framegraph compiler infers required transitions from declared uses: draw
passes write `SceneColorHDR` and related intermediate attachments, optional
postprocess/debug/UI passes move the current `FrameRecipe.PresentSource`, and
the canonical `Present` declaration writes the imported `Backbuffer`. The
post-pass `ColorAttachmentWrite -> Present` transition is emitted from
`RenderGraph::ImportBackbuffer`'s final-state contract; there is no recipe-local
barrier annotation or special backbuffer-write exception.

Tests should assert compiled graph/resource properties by pass label and resource
name, not transient allocation IDs or backend-native handles.

## Queue Affinity Contract

`RenderPassRecord::Queue` uses `RHI::QueueAffinity` as the canonical queue
vocabulary through the framegraph `RenderQueue` alias. The supported affinity
names are `Graphics`, `AsyncCompute`, and `Transfer`; the single-queue default
remains `Graphics`.

`RHI::ResolveQueueAffinity(requested, profile)` is the CPU-testable demotion
rule for optional queues: missing `AsyncCompute` or `Transfer` capability
demotes to `Graphics`, while `Graphics` never migrates away from graphics.
`PartitionPassesByQueue(...)` is a pure helper that buckets compiled live passes
by resolved affinity in deterministic `(topological rank, declared pass index)`
order and reports `QueueAffinityDemotedCount`.

The compiler also emits backend-neutral cross-queue timeline records for live
producer->consumer handoffs after culling. For each live pass on queue A that
consumes a resource last accessed by queue B (B != A), the compiled graph
contains one `CrossQueueTimelineSignal` on B, one `CrossQueueTimelineWait` on A,
and one `CrossQueueTimelineEdge` tying the pair together. Values are assigned
per producing queue in deterministic `(topological rank, declared pass index)`
order, so repeated compiles of the same graph produce byte-identical records.
Culled producer/consumer branches do not emit timeline records. A cyclic live
cross-queue dependency fails compilation with `RenderGraphValidationCode::CrossQueueCycle`
instead of producing a partial submit schedule.

Cross-queue resource ownership policy is compiled into the same barrier packets
as layout/access transitions. Live cross-queue transient resources are classified
as `QueueSharingMode::Concurrent`, so they keep the timeline edge plus normal
barriers and do not emit queue-family ownership transfers. Live cross-queue
imported resources are classified as retained `QueueSharingMode::Exclusive` and
emit paired `QueueOwnershipTransferKind::Release` / `Acquire` barriers through
the Sync2 packet path: the release packet is tagged `AfterPass` on the producer,
and the acquire packet is tagged `BeforePass` on the consumer. The packet fields
carry CPU-visible queue-family tokens derived from `RenderQueue`; concrete
Vulkan queue-family translation remains backend work.

This remains a CPU/null contract, not backend execution. Vulkan multi-queue
recording and opt-in `gpu;vulkan` smoke coverage are owned by `GRAPHICS-037D`.

## Default-Recipe `gpu;vulkan` Smoke

The opt-in default-recipe smoke coverage lives in
`tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`. It drives the
canonical recipe on promoted Vulkan hosts, asserts that the operational command
stream records canonical passes such as `Present`, and keeps the four-sample
backbuffer-readback parity harness under `gpu;vulkan;graphics`. The fixture is
selected only via `ctest -L 'gpu' -L 'vulkan' ...`; the default CPU gate excludes
it and the tests report `SKIPPED` when GLFW or a Vulkan-capable swapchain/device
is unavailable.
