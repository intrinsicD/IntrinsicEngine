# Graphics Framegraph

`src/graphics/framegraph` owns backend-agnostic render-graph resource declarations,
pass declarations, compilation, validation, transient allocation, barrier packet
inference, and execution ordering. It does not own renderer recipe selection,
backend command bodies, platform windows, swapchains, runtime extraction, live ECS
state, or asset-service traffic.

## Ownership boundaries

- `graphics/renderer` owns `FrameRecipe` typed pass/resource identities,
  recipe feature gates, canonical debug labels, renderer diagnostics, and
  pass-body command contracts.
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
`DescribeDefaultFrameRecipe(features)`. The recipe assigns typed
`FramePassId`/`FrameResourceId` values to recipe-owned passes and resources; the
framegraph stores those IDs on pass/resource declarations and rejects duplicate
non-zero IDs at compile time. `FrameRecipeIntrospection` exposes helper lookups
that map typed IDs to compiled pass/resource indices, and
`CompiledRenderGraph::{PassIds,TextureResourceIds,BufferResourceIds}` preserve
typed identities beside the human-readable names for renderer command routing,
resource binding, and diagnostics. Human-readable names remain stable
diagnostics and debug dump labels, not the correctness contract.
The framegraph treats recipe declarations as the single source of truth for
imported-resource write authorization, transient-resource lifetime,
dependency-driven pass ordering, and final backbuffer presentation. Default
recipe passes should rely on declared resource reads/writes for order wherever
that dependency is representable in the graph; explicit pass dependencies are
reserved for ordering constraints that resources do not model. The compiler
preserves those explicit edges in `CompiledPassDeclarations::ExplicitDependencyPasses`,
and debug dumps print them per pass so reviewers can distinguish intentional
side-effect ordering from resource-derived scheduling.

The framegraph compiler infers required transitions from declared uses: draw
passes write `SceneColorHDR` and related intermediate attachments, optional
postprocess/debug/UI passes move the current `FrameRecipe.PresentSource`, and
the canonical `Present` declaration writes the imported `Backbuffer`. The
post-pass `ColorAttachmentWrite -> Present` transition is emitted from
`RenderGraph::ImportBackbuffer`'s final-state contract; there is no recipe-local
barrier annotation or special backbuffer-write exception.
Read-only color attachment uses (`TextureUsage::ColorAttachmentRead`) compile to
a dedicated read barrier state that lowers to color-attachment layout with
`ColorAttachmentRead` access, so consecutive read-only color attachment accesses
do not emit artificial write-state transitions.

Tests should assert compiled graph/resource properties by typed recipe identity
where available, and may assert pass/resource names only for diagnostics and
debug dump stability. Tests must not depend on transient allocation IDs or
backend-native handles.

## Validation Result Contract

`RenderGraphCompiler::Compile(...)` reports structured compile diagnostics
through its optional `RenderGraphValidationResult*` out-param. Successful
compiles also copy the same findings into `CompiledRenderGraph::ValidationFindings`
so callers that only need the compiled payload can inspect warnings and
non-fatal errors without a separate side channel. Failed compiles publish their
diagnostics through the out-param before returning an error.

`RenderGraph::Compile()` is intentionally non-`const`: it updates graph-owned
last-compile diagnostics and transient allocation state. Callers using the
stateful `RenderGraph` wrapper can inspect diagnostics through
`RenderGraph::GetLastCompileValidationResult()`. The static compiler API has no
process-global or `thread_local` "last result" state.

## Reset/Redeclare Scratch Contract

`RenderGraph::Reset()` invalidates pass/resource handles by advancing the graph
generation, clears declared resources, and recycles pass records for the next
declaration pass. Recycled `RenderPassRecord` instances are reset before reuse
while retaining the capacity of their texture access, buffer access, and
explicit-dependency vectors. This keeps steady-state recipe redeclaration from
reallocating per-pass access storage while preserving the visible reset contract:
new compiles must not inherit stale pass names, dependencies, render-pass
metadata, resource accesses, or validation state from the prior frame.

Stateful `RenderGraph::Compile()` also owns private compiler scratch for
graph-analysis temporaries such as resource states, adjacency lists, live-pass
queues, queue handoff dedup sets, timeline synthesis scratch, and barrier-state
tracking. The static `RenderGraphCompiler::Compile(...)` API remains
self-contained and uses local scratch for one-shot callers. `CompiledRenderGraph`
continues to own its pass/resource names and output vectors by value; it must
not hold `string_view`s into mutable graph storage because compiled results can
outlive the `RenderGraph` instance or its next `Reset()`.

## Barrier Packet Traversal Contract

Compiled barrier packets are sorted by `(PassIndex, Stage)` using
`BarrierPacketStageSortKey`, where `BeforePass` precedes `AfterPass` for a pass.
Emitters use `FindBarrierPacketRange(...)` to visit only packets matching the
current pass/stage instead of rescanning the full packet list. Before emission,
`ValidateBarrierPacketBounds(...)` checks the full packet list against the
compiled graph's pass/resource handle counts so both the executor and renderer
preserve fail-closed behavior for malformed packet streams.

Renderer-side alias-reuse injection may add or remove packets after compile; it
must re-sort the packet list before execution so the shared range lookup remains
valid.

## Parallel Record/Join Contract

`RenderGraphExecutor::ExecuteParallelRecordJoin(...)` is the backend-neutral
CPU/null contract for parallel command recording. It groups live passes by
`CompiledRenderGraph::TopologicalLayerByPass`, records each layer through the
caller-provided record callback, and optionally dispatches same-layer records
onto `Core::Tasks` workers when the scheduler is initialized. The record
callback may run concurrently and must only touch pass-local state or
caller-provided synchronization.

After every record callback in a layer joins, the executor emits barrier and
submit callbacks in the same serial topological order as `Execute(...)`; GPU
visible submission order is unchanged. If any record callback fails, all
scheduled callbacks in that layer still join and the executor returns the first
error before emitting serial submit callbacks. The renderer uses the same
record/join primitive for accepted single-queue frames and for accepted CPU/null
multi-queue submit plans; multi-queue joins emit each recorded context through
the existing queue-submit batches so timeline waits/signals and barriers keep
their serial placement. The PR-fast
`rendering.rendergraph_parallel_recording.smoke` benchmark records checksum
parity plus serial/parallel CPU timings without making an adoption claim, and
`DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialReadbackWithValidation`
is the opt-in `gpu;vulkan` proof for the implemented graphics-queue secondary
command path. `DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialAsyncComputeReadbackWithValidation`
keeps postprocess enabled and proves accepted async-compute secondary command
contexts through the same serial/parallel readback parity harness.

## Transient Placement Contract

`RenderGraph::Compile()` computes a CPU-visible placement plan for every used
non-imported transient texture and buffer. Each
`TransientResourcePlacement` records the resource index, placement block, byte
offset, aligned size, alignment, and first/last use in topological execution
rank. Texture and buffer placements use separate block domains; the compiler uses
deterministic first-fit reuse of ranges whose prior occupant's execution lifetime
ended before the new occupant's first use.

The compiled graph reports both
`TransientNaiveMemoryEstimateBytes` (sum of aligned transient sizes without
reuse) and `TransientPlacedPeakMemoryEstimateBytes` (sum of planned placement
block peaks). The legacy `TransientMemoryEstimateBytes` field mirrors the
planned peak so existing diagnostics keep a single useful estimate. When
`SetTransientAliasingEnabled(false)` is selected, placement stays deterministic
but does not reuse ranges: planned peak equals the naive estimate and no
alias-reuse hazards are emitted.
Texture transient byte estimates come from
`RHI::EstimateTextureStorageBytes`, so block-compressed formats use the same
texel-block sizing as RHI upload/storage helpers instead of a framegraph-local
bytes-per-pixel table.

Alias reuse is represented in the barrier plan through
`TextureAliasReuseBarrierPacket` / `BufferAliasReuseBarrierPacket` entries on a
`BeforePass` packet for the first pass that uses the new occupant. Lifetimes are
computed in execution-rank space, but barrier packets are keyed back to real pass
indices before the renderer lowers them. With renderer transient aliasing enabled
and compatible backend memory requirements, the renderer binds non-imported
transients into placed memory blocks and submits those alias-reuse barriers before
the first pass that reuses a range. With aliasing disabled or unsupported, the
renderer clears alias barriers and uses per-resource frame-slot RHI allocations.
The opt-in compiler debug dump includes texture/buffer transient placement
tables plus alias-reuse barrier rows so backend smoke failures can cite the exact
reuse plan.

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

`BuildQueueSubmitPlan(...)` is the backend-neutral submit-plan seam. It walks the
compiled topological order, resolves each pass through a
`RHI::QueueCapabilityProfile`, groups contiguous passes into
`QueueSubmitBatch` records, and attaches per-batch cross-queue timeline
wait/signal records. When optional queues demote to `Graphics`, timeline edges
whose signal and wait queues resolve to the same queue are omitted and counted
through `OmittedSameQueueTimelineEdgeCount`. The renderer converts this plan to
`RHI::FrameQueueSubmitPlanDesc` and records each batch through
`IDevice::GetQueueSubmitContext(...)` when the backend accepts the plan. Concrete
`VkQueue` selection, command-buffer allocation, timeline semaphores, and queue
fences remain backend-owned work.

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

This remains a CPU/null scheduling contract. Vulkan multi-queue recording
consumes the contract through the RHI submit-plan seam, and opt-in `gpu;vulkan`
smoke coverage is owned by `GRAPHICS-037D`.

## Default-Recipe `gpu;vulkan` Smoke

The opt-in default-recipe smoke coverage lives in
`tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`. It drives the
canonical recipe on promoted Vulkan hosts, asserts that the operational command
stream records canonical passes such as `Present`, and keeps the four-sample
backbuffer-readback parity harness under `gpu;vulkan;graphics`. The fixture is
selected only via `ctest -L 'gpu' -L 'vulkan' ...`; the default CPU gate excludes
it and the tests report `SKIPPED` when GLFW or a Vulkan-capable swapchain/device
is unavailable.

`PostProcessHistogramPass` is the default recipe's compute-only async queue
probe. The recipe requests `RenderQueue::AsyncCompute` for that pass; the
framegraph/RHI resolver demotes it back to graphics on hosts without async
queue support. On promoted Vulkan hosts with async compute, the
`DefaultRecipeSurfaceGpuSmoke.AsyncComputeHistogramQueueReadbackMatchesMinimalHarnessSamples`
fixture asserts that `RenderGraphFrameStats::AsyncComputeUtilizedFrames >= 1`
and that the same four-sample backbuffer-readback parity harness still matches.
`DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialReadbackWithValidation`
disables the lighting and postprocess extension slots to keep the current
Vulkan plan graphics-only, then captures serial and parallel readback bytes
with validation enabled and asserts that the parallel frame accepted secondary
command contexts without a serial fallback.
`DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialAsyncComputeReadbackWithValidation`
keeps postprocess enabled, requires an operational async-compute queue profile,
then captures serial and parallel readback bytes with validation enabled and
asserts that accepted async-compute secondary command contexts did not fall
back to serial.
