# K-Means GPU backend — Framework24 → IntrinsicEngine migration proposal

Status: proposal / design note (not yet a promoted task).
Scope: analyze the Framework24 CUDA k-means and propose a parity-gated GPU
backend for `Geometry.KMeans` in IntrinsicEngine.

Related existing work:
- `docs/architecture/algorithm-variant-dispatch.md` — the CPU-reference + RHI
  overload seam that `Geometry.KMeans` already exemplifies.
- `docs/architecture/compute-parallel-primitives.md` — GRAPHICS-108 scan/compaction.
- `tasks/done/GEOM-052-shared-cpu-gpu-backend-seam-kmeans-exemplar.md` — installed
  the `{CPU, GPU}` seam and telemetry; explicitly deferred the real GPU kernel.
- `tasks/done/GRAPHICS-086-rhi-retirement-parity-and-cuda-decision.md` — recorded
  the CUDA remove/keep decision.
- `tasks/active/METHOD-013-progressive-poisson-disk-gpu-backend.md` — the live
  template for a Vulkan-compute method backend (module shape, slices, tests).

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
- **The seam is already wired.** `Extrinsic.Runtime.KMeansBackend::ClusterKMeans`
  has the exact `if (requestedBackend == GPU && device.IsOperational())` hook,
  currently a no-op comment: *"A real GPU KMeans kernel must be added by a later
  parity-gated backend task."* This proposal is that task.

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

## 4. Proposed architecture

Mirror METHOD-013 exactly. New runtime module
**`Extrinsic.Runtime.KMeansGpuBackend`** (`src/runtime/`), consumed by the
existing `Extrinsic.Runtime.KMeansBackend` GPU branch. Geometry stays RHI-free;
only the runtime adapter sees `RHI::IDevice`.

```
Geometry.KMeans (geometry, RHI-free)      ← canonical CPU reference (unchanged)
Extrinsic.Runtime.KMeansGpuBackend (runtime)
    ├─ buffer layout + BDA state record + push-constant structs
    ├─ pipeline set (reset / assign / update / reduce)
    ├─ persistent resource set (created once, reused every iteration)
    ├─ RecordKMeansGpuExecution(...)  → records the whole Lloyd loop
    └─ ReadKMeansGpuReadbacks(...)    → one batched drain at the end
Extrinsic.Runtime.KMeansBackend::ClusterKMeans(...)  ← existing seam, routes here
```

### 4.1 Shaders (`assets/shaders/`, `local_size_x = 256`, scalar BDA push)

Follow the promoted BDA convention (storage buffers reached through
`buffer_reference` from a scalar push-constant state record — identical to
`progressive_poisson_build_cells.comp` and the clustered-light shaders; **no
descriptor-set storage bindings**).

1. `kmeans_reset.comp` — zero `sums`, `counts`, and the reduction scratch.
2. `kmeans_assign.comp` — **the hot kernel** (see §5).
3. `kmeans_update.comp` — per cluster: `new = count>0 ? sum/count :
   points[farthestIndex]`; compute `shift² = |new-old|²`; `atomicMax` into a
   `maxShiftBits` slot; write into the ping-pong centroid buffer.
4. Convergence is read from the small state buffer (`changedCount`,
   `maxShiftBits`) — see §6.

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
| `Reduction scratch` | few | inertia + farthest-point argmax | per iter |
| `State` (BDA table) | 1 | pointers + `changedCount`, `maxShiftBits`, `iter` | resident |
| Host-visible readback | `n`+`n`+`k` | outputs | **drained exactly once at the end** |

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
  `ReadBuffer`; `IDevice::GetBufferDeviceAddress` for the BDA push table
  (requires `BufferUsage::Storage`).
- **Recording**: `ICommandContext::BindPipeline` → `PushConstants` → `Dispatch(⌈n/256⌉,1,1)`
  → `BufferBarrier(MemoryAccess::ShaderWrite, ShaderRead)` between passes →
  `ShaderWrite → HostRead` + `CopyBuffer` before readback. `DispatchIndirect`
  available for device-driven early-out.
- **Reductions**: reuse `Extrinsic.Graphics.ComputeParallelPrimitives` (prefix
  scan / compaction / count→dispatch-args) rather than reimplementing.
- **Shader idiom**: model on `assets/shaders/instance_cull.comp` +
  `assets/shaders/common/gpu_scene.glsl` (scalar push block of `uint64_t` BDAs,
  `GL_EXT_buffer_reference2`, `atomicAdd`). Shaders compile via
  `cmake/CompileShaders.cmake` (`glslc`, `--target-env=vulkan1.3`), validated by
  `tools/repo/check_shader_outputs.py`.

---

## 5. Making the assignment kernel fast

The assignment pass is `O(n·k)` and dominates. Key optimizations over the
Framework24 naive global-atomic kernel:

1. **Privatized accumulation in shared memory.** Each workgroup keeps a local
   copy of the `k` `sums`/`counts` in shared memory, `atomicAdd`s locally during
   assignment, then flushes once to global memory. This collapses global-atomic
   traffic from `O(n)` to `O(numGroups · k)` — typically a large speedup and the
   main win. Guard on `k · 16B ≤ shared-memory budget`; fall back to the direct
   global-atomic path when `k` is too large.
2. **Cache centroids in shared memory** for the scan so all threads in a group
   reuse them instead of re-reading global memory `k` times.
3. **SoA coalesced loads** for positions (keep Framework24's `d_x/d_y/d_z`
   layout; drop the AoS variant).
4. **Deterministic accumulation option.** Float atomics are order-nondeterministic.
   Offer scaled fixed-point (`int` atomics) or a two-pass tree reduction so
   results are reproducible and parity is tight; keep float atomics as the fast
   default with a declared tolerance.
5. **Large-`k` variant (later).** Port the `KmeansIndex` idea — a spatial
   structure over centroids — as an opt-in strategy when `k` is large. Not needed
   for the common `k ≤ few hundred` case where the shared-memory scan wins.

Fuse the per-iteration reduction (inertia sum + farthest-point argmax) into the
assign pass or a short follow-on reduce, so the empty-cluster reseed and
convergence data are ready without an extra full-buffer pass.

---

## 6. Eliminating per-iteration CPU↔GPU I/O in the Lloyd loop

Framework24 stalls the device twice per iteration. In Vulkan we keep the whole
loop on the GPU in **one command submission**:

- Record `MaxIterations × [reset → assign → update]`, chaining passes with
  `ICommandContext::BufferBarrier(ShaderWrite → ShaderRead | ShaderWrite)`
  between them (the same `RHI::MemoryAccess` barrier vocabulary GRAPHICS-108
  uses). No `WaitIdle`, no readback, no host round-trip inside the loop.
- **Early-out without host involvement:** the `update` pass writes
  `changedCount` and `maxShiftBits` into the resident `State` buffer. A cheap
  guard at the top of each subsequent `assign`/`update` pass reads a device-side
  `converged` flag and returns immediately, so post-convergence iterations become
  near-free no-ops. (A recorded command buffer can't `break`; guarding is the
  Vulkan-idiomatic equivalent. `DispatchIndirect` with a device-computed group
  count of 0 is an alternative.)
- Optionally split into a few submissions with a single readback of `State`
  between them to *actually* stop recording once converged, trading one
  round-trip per batch for a shorter command stream on high `MaxIterations`.

Recommended default: single-submission unrolled loop with device-side early-out
guard — **zero per-iteration I/O**, one final readback.

---

## 7. Parity, tests, benchmarks (backend-policy)

- **Declared tolerance.** Per `docs/methods/backend-policy.md`, parity thresholds
  must be declared. Compare `Inertia` within relative tolerance and `Centroids`
  within absolute tolerance; allow label differences only for near-equidistant
  ties. The deterministic fixed-point mode (§5.4) tightens this.
- **`gpu;vulkan` parity test** (`ci-vulkan`): GPU reproduces the CPU reference on
  shared fixtures; assert `ActualBackend == GPU` when operational.
- **Default-gate fallback test**: on the Null device, a `Backend::GPU` request
  returns the CPU result with `ActualBackend == CPU`, `FellBackToCPU == true`
  (extends the existing `tests/contract/runtime/Test.KMeansBackend.cpp`).
- **Benchmark**: add a manifest with `gpu_time_ms` and a CPU-vs-GPU speedup
  diagnostic (heavy/nightly), baseline-compared before any speedup claim.

---

## 8. Slice plan (mirrors METHOD-013)

- **Slice A — seam + telemetry (`CPUContracted`).** Add
  `Extrinsic.Runtime.KMeansGpuBackend` skeleton; route the existing
  `ClusterKMeans` GPU branch through it; honest fallback on non-operational
  device; default-gate fallback test. No kernel yet.
- **Slice B — layout + shaders (`CPUContracted`).** BDA state record,
  push-constant structs, `kmeans_reset/assign/update` shader assets, fail-closed
  dispatch planning; `check_shader_outputs` wiring. Execution still falls back.
- **Slice C — record + persistent buffers.** Persistent resource set with
  `BufferLease` caching, one-time upload (or retained-buffer alias), recorded
  Lloyd loop with barriers, batched readback ownership.
- **Slice D — parity + benchmark (`Operational` → `ParityProven`).** `gpu;vulkan`
  parity tests, deterministic-mode validation, benchmark manifest + baseline.

Each slice is independently bisectable and keeps the default CPU gate green.

---

## 9. Risks / open questions

- **Shared-memory `k` ceiling** → dual-path assign (privatized vs direct global
  atomic); pick by `k` at record time.
- **Float determinism vs speed** → ship both float-fast and fixed-point-exact
  accumulation; declare which the parity test uses.
- **Retained-buffer aliasing** depends on the point-cloud GPU buffer being
  created with `Storage` + BDA usage; if not, a one-time upload is the fallback
  (still zero per-iteration I/O). Confirm during Slice C.
- **Command-buffer length** for very large `MaxIterations` → cap unrolling and/or
  use the batched-submission early-out.
- **Seeding on GPU** is deferred; CPU seeding (one upload) matches the reference
  deterministically and is cheap. A GPU k-means++ pass is a later optimization.
