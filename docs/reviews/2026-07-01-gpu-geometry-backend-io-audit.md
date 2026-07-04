# GPU geometry-processing backend I/O & performance audit

Date: 2026-07-01
Scope: every GPU-backed geometry-processing path in the tree, audited against the
same rubric proposed for the k-means GPU backend — **minimal CPU↔GPU I/O**,
**persistent buffer reuse**, and **barrier-based (not stall-based)
synchronization**. Companion to
`docs/migration/kmeans-gpu-vulkan-compute-proposal.md`.

## Landscape

Across `src/geometry`, `src/runtime`, and `methods/`, only two geometry
algorithms have a GPU seam:

1. `Geometry.KMeans` + `Extrinsic.Runtime.KMeansBackend` — seam + telemetry only;
   the kernel is the pending proposal. Nothing to audit yet.
2. `progressive_poisson` + `Extrinsic.Runtime.ProgressivePoissonGpuBackend`
   (METHOD-013) — the only extant GPU geometry-processing backend.

Both stand on two shared substrates that any future geometry backend (k-means
included) will reuse:

- `Extrinsic.Graphics.ComputeParallelPrimitives` — scan / compaction / count→dispatch-args.
- `Runtime.GpuReadbackJob` + `Graphics.GpuTransfer` + `RHI.TransferQueue` — the
  async upload/readback machinery; `Graphics.GpuWorld` — the managed upload arena.

So "hold all other GPU variants to the same bar" concretely means: the
ProgressivePoisson backend and these two substrates. The rest of the `*Gpu*`
modules are rendering/asset-streaming infrastructure, not geometry processing.

## Verdicts

| Path | Buffer reuse | GPU I/O | Sync / speed |
|---|---|---|---|
| ProgressivePoisson GPU backend | GOOD | GOOD (structure) | GAP (recorded-path efficiency) |
| ComputeParallelPrimitives | GOOD | GOOD | GAP (workgroup scan) |
| GpuReadbackJob / GpuTransfer / GpuWorld | GOOD | GOOD (async path) | GAP (default path stalls) |

**Bottom line:** buffer reuse and one-upload / one-readback *structure* are
already solid everywhere — no allocation churn, no per-iteration host round
trips. The genuine gaps are one cross-cutting I/O-stall and a handful of
kernel/dispatch efficiency items.

---

## Finding 1 (cross-cutting, highest impact) — the default readback path stalls the whole device

The async readback machinery is genuinely non-blocking: `GpuReadbackJob::Execute`
schedules `ITransferQueue::DownloadBuffer` behind a `ShaderWrite→TransferRead`
barrier and returns a ticket; completion is *polled* via
`IsDelivered`/`DrainCompleted` (timeline/fence-gated) and applied deferred on the
main thread — no `WaitIdle` anywhere in that path
(`Runtime.GpuReadbackJob.cpp:214-269`, `Graphics.GpuTransfer.cpp:187-264`,
`RHI.TransferQueue.cppm:44-48`).

But the *easy default* — `IDevice::ReadBuffer` — is contractually and actually a
full stall: its contract says "Backends MUST `WaitIdle()` … on entry"
(`RHI.Device.cppm:145-147`) and the Vulkan backend does `vkDeviceWaitIdle` on
every call plus a per-call staging `vmaCreateBuffer`
(`Backends.Vulkan.Device.cpp:3889-3937`). Because the async path requires wiring
`GpuTransfer` + `GpuReadbackJob` + the `DerivedJob` scheduler, backends drift to
the stalling default: **ProgressivePoisson collects its readbacks via
`device.ReadBuffer`** (`Runtime.ProgressivePoissonGpuBackend.cpp:595,1440-1442`),
inheriting a `vkDeviceWaitIdle` per solve even though its record/upload/readback
*structure* is otherwise exemplary.

Impact: for a one-shot solve the stall is a single end-of-run hitch (tolerable);
for any interactive or repeated-solve loop it serializes CPU and GPU.

Recommendations:
- Add a thin async-readback helper ergonomic enough that geometry backends adopt
  it instead of `IDevice::ReadBuffer` (the friction is why the stall wins).
- Pool the readback destination in `GpuReadbackJob` — today each submit
  heap-allocates a fresh `std::vector<std::byte>`
  (`Runtime.GpuReadbackJob.cpp:177-178,213`); let callers supply a reusable span.
- **k-means design rule:** the k-means backend must collect its final
  labels/distances/centroids through the async ticket path, never
  `IDevice::ReadBuffer`. Folded into the proposal.

## Finding 2 — ProgressivePoisson recorded-path efficiency (owned by METHOD-013)

Structure is good (all buffers allocated once before the level loop
`...cpp:393-497,1359`; persistent compaction scratch reused across both
compaction calls `:468-471,1261,1284`; bulk uploads `:529-539`; single
end-of-stream readback `:1388`; per-pass `BufferBarrier(ShaderWrite→ShaderRead)`
`:1219-1248`; whole algorithm in one command context; correct `ceil(n/256)`
sizing). The gaps are kernel/dispatch efficiency:

- **~4× over-dispatch.** `MakeMethodDispatch` sizes *every* pass by
  `max(elementCount, hashCapacity)` (`...cpp:210-211`); with `HashLoadFactor
  = 0.25` the hash table is ~4×N, so AcceptPhase and compaction launch ~4× the
  groups they need, the surplus only hitting the `globalIndex >= RemainingCount`
  early-out. → size only BuildCells by `hashCapacity`; size the rest by
  `elementCount`.
- **No shrinking / indirect dispatch across levels.** `RemainingCount` is pinned
  to `InputCount` for all levels (`...cpp:786-787`) and the remaining ping-pong
  is not yet consumed, so every level does full-N work regardless of how many
  points remain. → drive per-level group counts via `DispatchIndirect` off the
  compacted `OutputCount`.
- **Redundant global load in the hottest kernel.** `lookupCell` reads
  `hashKeys.Values[slot]` twice per probe (`accept_phase.comp:105-106`), doubled
  across 9/27 neighbor probes per invocation. → load the slot key once into a
  register.

These sit inside METHOD-013's active scope (its own Slice C/D own the
shrinking-dispatch and parity work). **Flag, do not edit** — captured here and
should be linked from that task rather than fixed out-of-band.

## Finding 3 — ComputeParallelPrimitives, and the primitive k-means still needs

Buffer reuse and I/O are GOOD: caller-provided scratch reused when valid, a
single contiguous scratch buffer sub-addressed per recursion level (no per-level
buffers), owned-lease fallback only when the caller omits scratch
(`...cpp:941-961`); count readback is an opt-in follow-up step and dispatch-indirect
args are published entirely on-GPU (`parallel_count_to_dispatch_args.comp:47-49`).

Gaps:
- **Workgroup scan is Hillis-Steele with two `barrier()`s per step** (16 barriers
  for 256 lanes), O(n log n) work (`parallel_prefix_scan.comp:74-80`). → move to
  `subgroupInclusive/ExclusiveAdd` or a work-efficient Blelloch scan to roughly
  halve the barriers and shared-memory traffic. This is the only material
  performance gap in the shared substrate.
- **uint32-only accumulation** can silently overflow at scale; the CPU reference
  guards this, the GPU path does not (`...cpp:617-623`).

Fit for k-means: at audit time it was a good substrate for the *assignment-compaction and
indirect-dispatch* half (counting-sort offsets, variable-width follow-up passes
with no CPU sync), but it had **no float segmented/per-cluster reduction** — the
actual centroid accumulate-and-divide step. GRAPHICS-111 added a deterministic
segmented float sum/count/mean primitive under
`Extrinsic.Graphics.ComputeParallelPrimitives`; k-means integration still needs a
separate task to consume it. Folded into the proposal's §5.

---

## Recommended follow-ups (not applied here — each is its own task/PR)

1. **Async-readback ergonomics + pooled destination** (shared infra). Removes the
   `vkDeviceWaitIdle` default for all geometry backends. Highest leverage.
2. **Float segmented-reduction primitive** in `ComputeParallelPrimitives` for
   centroid-style means. Retired by GRAPHICS-111; k-means §5 now depends on a
   consumer integration task rather than a primitive gap.
3. **Work-efficient workgroup scan** (subgroup/Blelloch) + uint32-overflow note in
   `ComputeParallelPrimitives`.
4. **ProgressivePoisson dispatch-sizing + indirect-shrink + hash-probe** items —
   route into METHOD-013's remaining slices, do not patch out-of-band.

Items 1–2 are prerequisites for the k-means backend being "as fast as possible";
they are reflected as design rules in the proposal so k-means adopts the async
readback path and a real reduction primitive from the start rather than repeating
ProgressivePoisson's `ReadBuffer` drift.
