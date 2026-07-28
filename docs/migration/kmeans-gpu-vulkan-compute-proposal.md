# K-Means GPU backend — Framework24 → IntrinsicEngine migration proposal

Status: implemented design note; GEOM-056 Slices A-D implemented the planning,
recording, persistent-buffer upload, post-submit async readback, portable
assignment/update shaders, opt-in `gpu;vulkan` parity smoke, and benchmark
manifest/result path. RUNTIME-196 subsequently converged the public architecture:
`ClusteringService::RunKMeans` is now the sole CPU/GPU operation, while the
recorder, resource cache, shared readback adapter, and one GPU participant are
private `ClusteringModule` state. Sandbox UI/config/agent callers, the Vulkan
smoke, and the benchmark all use that operation. The current promoted shader
path avoids optional Vulkan float-atomic and int64-atomic feature requirements;
the faster segmented-reduction path remains a follow-up.
Scope: analyze the Framework24 CUDA k-means and propose a parity-gated GPU
backend for `Geometry.KMeans` in IntrinsicEngine.

Related existing work:
- `docs/architecture/algorithm-variant-dispatch.md` — the CPU-reference plus
  sole typed runtime-operation pattern that `Geometry.KMeans` exemplifies.
- `docs/architecture/compute-parallel-primitives.md` — GRAPHICS-108 scan/compaction.
- `tasks/archive/GEOM-052-shared-cpu-gpu-backend-seam-kmeans-exemplar.md` — installed
  the `{CPU, GPU}` seam and telemetry; explicitly deferred the real GPU kernel.
- `tasks/archive/GRAPHICS-086-rhi-retirement-parity-and-cuda-decision.md` — recorded
  the CUDA remove/keep decision.
- `tasks/active/METHOD-013-progressive-poisson-disk-gpu-backend.md` — the live
  template for a Vulkan-compute method backend (module shape, slices, tests).
- `tasks/archive/GEOM-056-kmeans-gpu-vulkan-compute-backend.md` — implemented and
  parity-gated this KMeans Vulkan-compute backend.

---

## 1. Backend choice: Vulkan compute, not CUDA

**Recommendation: Vulkan compute.** This is not a close call for IntrinsicEngine:

- **The CUDA decision is already on record.** GRAPHICS-086 formally **removed**
  CUDA from the promoted path: "No current runtime, graphics, method, or
  benchmark consumer requires a CUDA compute seam; future CUDA work must open a
  new opt-in method/backend task with a concrete workload and verification plan."
  `INTRINSIC_ENABLE_CUDA` defaults `OFF` and gates only legacy modules.
- **The engine is Vulkan-native.** RHI (`Extrinsic.RHI.*`), the Vulkan backend
  (volk + VMA), the `ci-vulkan` gate, and the `gpu;vulkan` test label already
  exist. A CUDA backend would need nvcc/thrust through vcpkg, break on
  non-NVIDIA hosts, and cannot run under the existing GPU CI.
- **Precedent is unambiguous.** METHOD-013 states its GPU backend is "Vulkan
  compute only; no CUDA path," and the dispatch doc maps `Backend::GPU →
  gpu_vulkan_compute` for compute-style families.
- **The operation is already wired.**
  `Extrinsic.Runtime.ClusteringService::RunKMeans` preserves honest backend and
  fallback telemetry while private `ClusteringModule` state owns command
  recording, pipelines, persistent buffers, and async readback.

A straight port of the Framework24 CUDA kernels is therefore the wrong shape for
this repo. We migrate the **algorithm**, not the CUDA mechanics.

---

## 2. What Framework24 does (`lib_bcg_framework/src/cuda/src/bcg_kmeans.cu`)

Three GPU variants, all Lloyd's algorithm on 3D points:

| Variant | Layout | Assignment | Notes |
|---|---|---|---|
| `Kmeans` | SoA (`d_x/d_y/d_z`) | one thread/point, linear scan over `k` | baseline |
| `KmeansVectorNaive` | AoS `Vector<float,3>` | same, vectorized | |
| `KmeansIndex` | AoS | LBVH over **centroids**, nearest query | wins for large `k` |

Per-iteration structure (all three):
1. `thrust::fill` zeroes `sums` and `counts`.
2. `assign_clusters<<<blocks,1024>>>`: each point loads `x`, scans `k` centroids
   with squared-L2, writes `label` + `distance`, then `atomicAdd`s its position
   into the per-cluster `sums` and `atomicAdd`s `counts`.
3. `cudaDeviceSynchronize()`.
4. `compute_new_means<<<1,k>>>`: each cluster thread sets `mean = sum / max(1,count)`.
5. `cudaDeviceSynchronize()`.

Final `thrust::reduce` for total error; results copied back to host.

### Strengths
- Correct data-parallel decomposition (point-parallel assign, cluster-parallel
  update) — the right skeleton to carry over.
- The SoA layout gives coalesced loads.
- The LBVH variant is a genuinely good idea when `k` is large (turns the `O(k)`
  scan into `O(log k)`).

### Weaknesses to *not* carry over
1. **Setup/readback I/O is element-by-element.** Positions are uploaded through
   `thrust::device_vector::operator[]` in a host loop (one transfer per element),
   and results are reconstructed the same way. This dominates wall-clock for
   anything but toy inputs and is pure avoidable I/O. It also builds an unused
   `ps` (`float4`) host array.
2. **Two full device stalls per iteration** (`cudaDeviceSynchronize` ×2). No
   async, no overlap, no persistent command stream.
3. **Global-atomic contention.** Every one of `n` points `atomicAdd`s into only
   `k` global accumulators — contention scales with `n/k`. This is the single
   biggest kernel-time inefficiency.
4. **No convergence test.** It always runs the full `max_iterations`, even after
   labels stop changing.
5. **Empty-cluster handling differs from the IntrinsicEngine reference.**
   `mean = sum / max(1,count)` collapses an empty cluster's centroid toward the
   origin (`0/1`). The IE CPU reference instead **reseeds an empty cluster to the
   globally farthest point** (`points[maxDistanceIndex]`). Parity must follow the
   **IE reference**, not Framework24.
6. `compute_new_means<<<1,k>>>` uses a single block (caps `k ≤ 1024`,
   underutilizes the GPU).

---

## 3. Target semantics — the IntrinsicEngine CPU reference is canonical truth

`src/geometry/Geometry.KMeans.cpp` (`Cluster`) is the parity oracle
(AGENTS.md §6, "reference is canonical truth"). The GPU backend must reproduce,
within a declared tolerance:

- **Seeding**: `Random` (seeded shuffle) or `Hierarchical` (farthest-point from
  running mean), or externally supplied centroid seeds when `≥ k` finite seeds
  are given.
- **Assignment**: nearest centroid under squared Euclidean distance; record
  `Labels[i]` and `SquaredDistances[i]`.
- **Update**: `centroid = count>0 ? sum/count : points[MaxDistanceIndex]`
  (empty-cluster reseed to the global farthest sample).
- **Diagnostics**: `Inertia = Σ bestDistance`, `MaxDistanceIndex = argmax
  bestDistance`, `Iterations`, `Converged`.
- **Convergence**: stop when **no label changed** OR **max centroid shift² ≤
  tol²** (`tol = ConvergenceTolerance`).
- **Robustness**: empty input / `k==0` → `nullopt`; clamp `k ≤ n`.

Result payload (`KMeansResult`) and `RequestedBackend / ActualBackend /
FellBackToCPU` telemetry are already defined and must be filled honestly.

---

## 4. Implemented architecture

Geometry stays RHI-free. Runtime exports one feature service operation; only a
non-exported `ClusteringModule` partition and its owned GPU state see
`RHI::IDevice`, command recording, pipelines, buffers, and readback.

```
Geometry.KMeans (geometry, RHI-free)      ← canonical CPU reference (unchanged)
Extrinsic.Runtime.ClusteringModule (runtime)
    ├─ ClusteringService::RunKMeans(...)  → sole typed CPU/GPU operation
    ├─ CPU snapshot job + common stale/cancellation/writeback gate
    └─ private :GpuBackend partition + GPU state
        ├─ buffer layout + BDA state record + push-constant structs
        ├─ pipeline set (reset / assign / update / reduce)
        ├─ persistent resource cache keyed by `(n,k)`
        ├─ private recorder             → records the whole Lloyd loop
        └─ typed shared readback        → one post-submit result batch at the end
```

### 4.1 Shaders (`assets/shaders/`, `local_size_x = 256`, scalar BDA push)

Follow the promoted BDA convention (storage buffers reached through
`buffer_reference` from a scalar push-constant state record — identical to
`progressive_poisson_build_cells.comp` and the clustered-light shaders; **no
descriptor-set storage bindings**).

1. `kmeans_reset.comp` — zero the compatibility accumulator spans and reduction
   scratch (`maxShiftBits` plus legacy fields kept in the BDA record).
2. `kmeans_assign.comp` — one thread per point: scan centroids, write label, and
   write squared distance. It deliberately does not use
   `GL_EXT_shader_atomic_float`, `GL_EXT_shader_atomic_int64`, float atomics, or
   64-bit atomics, so shader-module creation does not require optional device
   features beyond the engine's baseline BDA/int64 requirements.
3. `kmeans_update.comp` — one thread per cluster: scan current labels and
   positions to compute `sum/count`, or reseed an empty cluster from the farthest
   point in the squared-distance buffer. It computes `shift² = |new-old|²`,
   `atomicMax`s that value as a 32-bit integer into `maxShiftBits`, and writes the
   updated centroid in place. A `NextCentroids` slot remains reserved for a future
   double-buffered variant.
4. Final diagnostics (`Inertia`, farthest point) are reconstructed from the
   read-back labels/distances/centroids in `KMeansGpuResultReadback::Collect`.

### 4.2 Buffers — persistent, allocated once, reused every iteration

This is the core of the "reuse all GPU buffers / avoid GPU I/O" requirement. All
buffers are `RHI::BufferManager::BufferLease`s created **once** per solve (and
cacheable across solves keyed by `(n, k)`), `BufferUsage::Storage |
TransferSrc | TransferDst`, reached by BDA:

| Buffer | Size | Role | Lifetime |
|---|---|---|---|
| `PositionX/Y/Z` (SoA f32) | `n` | input points | upload once (or **alias** the retained point-cloud GPU buffer → *zero upload*) |
| `Centroids A/B` (SoA f32) | `k` | ping-pong centroids | seeded once, swapped per iter |
| `SumX/Y/Z` | `k` | accumulators | cleared per iter on GPU |
| `Counts` (u32) | `k` | per-cluster counts | cleared per iter on GPU |
| `Labels` (u32) | `n` | output | resident, drained once |
| `SquaredDistances` (f32) | `n` | output | resident, drained once |
| `Reduction scratch` | few | `maxShiftBits` from update, plus legacy-compatible fields retained in the record | cleared per iter on GPU |
| `State` (BDA table) | 1 | device-address pointer table to the other buffers | resident |
| Copied result batch | `n`+`n`+`k` | outputs | **drained exactly once at the end** through `Graphics.GpuTransfer` after the producing submission has retired |

**I/O budget:** one bulk upload of positions + one batched readback of
`Labels`/`SquaredDistances`/`Centroids`. Nothing is mapped, copied, or synced
between iterations. Contrast Framework24: `O(n)` per-element transfers on entry
and exit plus two device stalls per iteration.

The primary input path is method-owned: `Geometry::Cloud` stores positions SoA
in a `PropertySet`, so `"v:point"` is a contiguous `std::span<const glm::vec3>`
that uploads in one `IDevice::WriteBuffer` into a device-local `Storage |
TransferDst` buffer (exactly what `Runtime.ProgressivePoissonGpuBackend` does
with `std::span<const glm::vec3> Positions`). This keeps the backend hermetic
and testable headless.

> A retained point cloud is *also* resident on the GPU for rendering, but via
> the renderer's `Graphics.GpuWorld` managed vertex arena, which is
> rendering-oriented and not a clean compute-alias target. Treat aliasing it as
> a later, optional optimization — not the baseline. The baseline (one upload,
> full per-iteration residency, cross-solve buffer caching) already satisfies
> the zero-per-iteration-I/O goal.

### 4.3 Concrete RHI building blocks (all confirmed present)

- **Pipelines**: `RHI::PipelineDesc{ .ComputeShaderPath = "shaders/kmeans_assign.comp.spv",
  .PushConstantSize = sizeof(push) }` (non-empty compute path → compute
  pipeline; push cap 256B) via `IDevice::CreatePipeline` / `RHI::PipelineManager`
  (ref-counted leases + hot-reload).
- **Buffers**: `RHI::BufferManager::Create({.SizeBytes, .Usage = Storage |
  TransferSrc | TransferDst})` → `BufferLease`; `IDevice::WriteBuffer` /
  `Graphics.GpuTransfer`; `IDevice::GetBufferDeviceAddress` for the BDA push table
  (requires `BufferUsage::Storage`).
- **Recording**: `ICommandContext::BindPipeline` → `PushConstants` → `Dispatch(⌈n/256⌉,1,1)`
  → `BufferBarrier(MemoryAccess::ShaderWrite, ShaderRead)` between passes →
  one multi-range `Graphics.GpuTransfer` batch records the deduplicated
  `ShaderWrite → TransferRead` bracket for final non-blocking drains after the
  compute-producing command submission has
  retired. `DispatchIndirect` remains available for a future device-driven
  early-out.
- **Reductions**: reuse `Extrinsic.Graphics.ComputeParallelPrimitives` (prefix
  scan / compaction / count→dispatch-args) rather than reimplementing.
- **Shader idiom**: model on `assets/shaders/instance_cull.comp` +
  `assets/shaders/common/gpu_scene.glsl` (scalar push block of `uint64_t` BDAs,
  `GL_EXT_buffer_reference2`). Shaders compile via
  `cmake/CompileShaders.cmake` (`glslc`, `--target-env=vulkan1.3`), validated by
  `tools/repo/check_shader_outputs.py`.

---

## 5. Making the assignment/update path fast

The current promoted path is intentionally portable: assign is `O(n·k)` and the
update pass scans `n` points once per cluster. That avoids optional Vulkan
float-atomic and int64-atomic requirements, but it is not the final performance
shape. Key follow-up optimizations over this portable baseline and over the
Framework24 naive global-atomic kernel:

1. **Segmented/per-cluster reduction.** GRAPHICS-111 adds
   `Extrinsic.Graphics.ComputeParallelPrimitives` support for deterministic
   per-segment float sums, counts, and count-normalized means without optional
   float atomics. A future fast path can keep a local copy of the `k`
   `sums`/`counts` in shared memory, use capability-gated float atomics or
   privatized accumulation locally during assignment, then flush once to global
   memory. Guard any such path on `k · 16B ≤ shared-memory budget`; keep the
   deterministic path as the missing-feature fallback.
2. **Cache centroids in shared memory** for the scan so all threads in a group
   reuse them instead of re-reading global memory `k` times.
3. **SoA coalesced loads** for positions (keep Framework24's `d_x/d_y/d_z`
   layout; drop the AoS variant).
4. **Deterministic accumulation option.** Float atomics are order-nondeterministic
   and feature-optional. Offer scaled fixed-point (`int` atomics) or a two-pass
   tree reduction so results are reproducible and parity is tight; any
   float-atomic path must be capability-gated before pipeline creation.
5. **Large-`k` variant (later).** Port the `KmeansIndex` idea — a spatial
   structure over centroids — as an opt-in strategy when `k` is large. Not needed
   for the common `k ≤ few hundred` case where the shared-memory scan wins.

Fuse the per-iteration reduction (inertia sum + farthest-point argmax) into the
segmented-reduction path or a short follow-on reduce, so the empty-cluster reseed
and convergence data are ready without the current `k` full-buffer scans.

> **Reuse `ComputeParallelPrimitives` where it fits.** Its compaction +
> on-GPU count→dispatch-indirect publication are a good fit for a counting-sort
> assignment and variable-width follow-up passes. GRAPHICS-111 now also provides
> the float segmented/per-cluster reduction primitive for the centroid
> accumulate-and-divide step, with a deterministic no-float-atomic fallback. The
> current k-means GPU execution still uses the portable per-cluster scan until a
> dedicated integration task switches it to this primitive. See the audit
> (`docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md`, Finding 3).

---

## 6. Eliminating per-iteration CPU↔GPU I/O in the Lloyd loop

Framework24 stalls the device twice per iteration. In Vulkan we keep the whole
loop on the GPU in **one command submission**:

- Record `MaxIterations × [reset → assign → update]`, chaining passes with
  `ICommandContext::BufferBarrier(ShaderWrite → ShaderRead | ShaderWrite)`
  between them (the same `RHI::MemoryAccess` barrier vocabulary GRAPHICS-108
  uses). No `WaitIdle`, no readback, no host round-trip inside the loop.
- **Current convergence behavior:** the command stream records the requested
  `MaxIterations`; final labels, distances, and centroids are drained once at the
  end. This preserves zero per-iteration CPU I/O and avoids unsafe intermediate
  readbacks. Device-side early-out remains a future optimization once the
  reduction primitive can publish `changedCount`/`maxShiftBits` and gate later
  dispatches without optional shader features.
- Optionally split into a few submissions with a single readback of `State`
  between them to *actually* stop recording once converged, trading one
  round-trip per batch for a shorter command stream on high `MaxIterations`.

Recommended default today: single-submission unrolled loop — **zero
per-iteration I/O**, one final readback. Future default: add device-side early
out after GRAPHICS-111 supplies the reduction data without reintroducing
ungated optional atomics.

> **Collect that final readback through the async path, not `IDevice::ReadBuffer`.**
> The audit found that `IDevice::ReadBuffer` contractually does `vkDeviceWaitIdle`
> per call (`RHI.Device.cppm:145-147`, `Backends.Vulkan.Device.cpp:3889-3937`),
> and the ProgressivePoisson backend drifted onto that stalling default despite
> otherwise-exemplary structure. k-means must route its final
> labels/distances/centroids drain through one copied multi-range
> `Graphics.GpuTransfer` batch (ticket → ready → exact consume) after the producing
> compute submission has retired, so the drain does not race the producer and the
> device never stalls — critical for interactive or repeated-solve use. See the audit
> (`docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md`, Finding 1).

---

## 7. Parity, tests, benchmarks (backend-policy)

- **Declared tolerance.** Per `docs/methods/backend-policy.md`, parity thresholds
  must be declared. Compare `Inertia` within relative tolerance and `Centroids`
  within absolute tolerance; allow label differences only for near-equidistant
  ties. The deterministic fixed-point mode (§5.4) tightens this.
- **`gpu;vulkan` parity test** (`ci-vulkan`): GPU reproduces the CPU reference on
  shared fixtures; assert the explicit GPU execution surface reaches the GPU path
  when operational and matches labels, centroids, inertia, and max-distance
  index within tolerance.
- **Default-gate fallback test**: on the Null device, a Vulkan-compute
  `RunKMeans` request completes through the CPU reference with requested/actual
  backend and fallback diagnostics asserted by `Test.ClusteringModule.cpp`.
- **Benchmark**: `IntrinsicKMeansGpuBenchmarkSmoke` invokes
  `ClusteringService`, reports end-to-end service command-to-applied-event time,
  CPU-reference baseline timing, committed-property/service counters, parity
  diagnostics, and `speedup_claimed=false`.

---

## 8. Implemented slice history

- **Slice A — seam + telemetry (`CPUContracted`).** GEOM-056 introduced the
  initial GPU planning seam and honest fallback on a non-operational device.
- **Slice B — layout + shaders (`CPUContracted`).** BDA state record,
  push-constant structs, `kmeans_reset/assign/update` shader assets, fail-closed
  dispatch planning; `check_shader_outputs` wiring. Execution still falls back.
- **Slice C — record + persistent buffers.** Implemented persistent `(n,k)`
  buffer leasing, one-time SoA position + seed-centroid upload, the recorded
  Lloyd loop with barriers, and async labels/distances/centroids drains. Those
  details now live in the private clustering backend partition. The current
  shaders use portable assignment plus per-cluster scans and do not require
  optional float/int64 atomic features.
- **Slice D — parity + benchmark (`Operational` → `ParityProven`).** Implemented
  by `ClusteringServiceGpuSmoke` and `IntrinsicKMeansGpuBenchmarkSmoke`: the
  opt-in `gpu;vulkan` smoke validates the deterministic fixture against the CPU
  reference, and the benchmark manifest/result report service latency,
  CPU-reference baseline timing, and parity diagnostics without claiming a
  speedup.
- **RUNTIME-196 convergence.** Moved the implementation behind
  `ClusteringService`, migrated UI/config/agent/smoke/benchmark callers, and
  deleted the public wrapper, Sandbox queue, and duplicate facade DTO family.

Each slice is independently bisectable and keeps the default CPU gate green.

---

## 9. Risks / open questions

- **Shared-memory `k` ceiling** → future dual-path reduction (privatized vs
  deterministic two-pass/fixed-point fallback); pick by `k` and feature support
  at record time.
- **Float determinism vs speed** → ship both a feature-gated float-fast path and
  fixed-point/two-pass deterministic accumulation; declare which the parity test
  uses.
- **Retained-buffer aliasing** depends on the point-cloud GPU buffer being
  created with `Storage` + BDA usage; if not, a one-time upload is the fallback
  (still zero per-iteration I/O). Confirm during Slice C.
- **Command-buffer length** for very large `MaxIterations` → cap unrolling and/or
  use the batched-submission early-out.
- **Seeding on GPU** is deferred; CPU seeding (one upload) matches the reference
  deterministically and is cheap. A GPU k-means++ pass is a later optimization.
