# Compute Parallel Primitives

Status: canonical for the `GRAPHICS-108` scan/compaction seam, the
`GRAPHICS-111` segmented float reduction seam, and the `GRAPHICS-112`
work-efficient scan/overflow guard.

`Extrinsic.Graphics.ComputeParallelPrimitives` owns generic `uint32` prefix-scan,
stream-compaction, count-publication, and deterministic float segmented-reduction
building blocks for GPU-oriented methods. The seam lives in `graphics` and
imports RHI contracts only; it must not import ECS, runtime, platform, app,
method packages, live asset services, or Vulkan-native handles.

## Current Contract

The default CPU-supported gate owns two contracts:

- deterministic CPU reference helpers for exclusive/inclusive prefix scan,
  stable compaction by flags, and float sum/count/mean reduction by segment;
- backend-neutral GPU dispatch planning, RHI command recording, and compacted
  count publication for Vulkan compute execution;
- a deterministic segmented float reduction record path that writes
  per-segment sums, counts, and count-normalized means. Empty segments produce
  count `0` and mean
  `kParallelSegmentedFloatReductionMeanForEmptySegment` (`0.0f`).

CPU prefix scan reports `SumOverflow` before a `uint32` accumulation can wrap.
The operational GPU shader cannot surface that post-dispatch status through the
unchanged record API, so its defined overflow guard is saturating: local scan
values, recursively published block sums, and add-offset fixups clamp to
`UINT32_MAX` on overflow. This keeps the GPU path fail-stable and prevents
silent wrap while preserving the public scan/compaction API and scratch layout.

The GPU record API remains fail-closed on unsupported devices: non-operational
devices report `DeviceUnavailable`, missing caller-owned recorder dependencies
report `InvalidInput`, and invalid handles, BDAs, pipelines, or undersized
scratch buffers report `InvalidGpuResource`. Operational Vulkan paths record
commands through `RHI::ICommandContext`; no Vulkan-native type leaks through the
graphics API.

Segmented float reduction uses a declared parity tolerance
(`kParallelSegmentedFloatReductionParityTolerance`, currently `1.0e-5f`) for
GPU-vs-CPU comparisons. The shipped GRAPHICS-111 path is the deterministic
fallback path: one workgroup owns one segment and walks the key/value stream in a
fixed order, so it does not require optional float-atomic Vulkan features and
does not create float-atomic pipelines.

Compaction count publication is an explicit follow-up recording step. Callers
can copy `OutputCount` into a host-visible readback buffer, build a
`ParallelDispatchIndirectArgs` buffer with `ceil(OutputCount / GroupSize)`, or
do both in one command stream. The dispatch-args buffer is published as
`IndirectRead` so downstream GPU consumers can call `DispatchIndirect` without a
CPU round trip.

## Shader Assets

GRAPHICS-108/111/112 pin five shader assets under `assets/shaders/`:

- `parallel_prefix_scan.comp` performs one 256-lane workgroup-local scan with
  subgroup arithmetic, scans the small per-subgroup totals in shared memory, and
  optionally writes one saturated block sum per workgroup. For stream
  compaction, the recorder sets a mode bit that normalizes nonzero flags to `1`
  before scan so GPU compaction matches the CPU reference's "nonzero means keep"
  contract.
- `parallel_scan_add_offsets.comp` adds recursively scanned block offsets back
  into an existing scan output with the same `UINT32_MAX` saturation guard.
- `parallel_compact_by_flags.comp` scatters kept keys using exclusive prefix
  offsets and writes the compacted count.
- `parallel_count_to_dispatch_args.comp` converts a compacted `uint32` count
  into the Vulkan dispatch-indirect argument schema `{groupCountX, 1, 1}`.
- `parallel_segmented_float_reduce.comp` assigns one 256-lane workgroup per
  segment, performs fixed-order lane-local scans over the key/value stream, and
  writes per-segment `float` sums, `uint32` counts, and `float` means.

All five use the promoted Buffer Device Address convention: storage buffers are
passed through scalar push constants, matching the clustered-light and culling
compute shaders. They do not introduce descriptor-set storage-buffer bindings.

## Scratch Layout

Prefix scan scratch contains one `uint32` array per recursive block-sum level.
For `ElementCount = N` and `GroupSize = 256`, level 0 stores
`ceil(N / 256)` block sums. Higher levels repeat that rule until the next level
would contain one element. Each level records an offset and size in bytes in the
dispatch plan.

Stream compaction scratch starts with an exclusive prefix-offset array of
`N * sizeof(uint32)` bytes. Recursive block-sum levels follow immediately after
that prefix-offset array. The scatter pass reads the flags and offsets, writes
`OutputKeys`, and publishes `OutputCount`.

The deterministic segmented float reduction path currently requires no scratch:
the dispatch plan records `ScratchBytes = 0`, while still returning the same
record-result scratch fields used by scan/compaction so a later scratch-backed
or feature-gated fast path can reuse the seam without changing callers.

## Dispatch And Barriers

Prefix scan planning emits:

1. one `PrefixBlockScan` over the source values;
2. zero or more recursive `PrefixBlockScan` passes over scratch block sums;
3. top-down `PrefixAddBlockOffsets` passes;
4. a final `Output` publication barrier.

Stream compaction planning emits the same exclusive scan sequence over `Flags`,
then one `StreamCompactScatter` pass.

Segmented float reduction planning emits one `SegmentedFloatReduce` dispatch
with `GroupCountX = SegmentCount`. For non-empty inputs the dispatch reads
`Keys` and `Values`; for empty inputs it still records a dispatch so sums,
counts, and means are zeroed for every segment.

The recorder turns that plan into RHI commands by binding one of the
caller-provided compute pipelines, pushing the matching scalar BDA push-constant
block, and dispatching the planned group count. Scratch is either
caller-provided or allocated as an owned `RHI::BufferManager::BufferLease`
returned in the record result so its lifetime spans command execution.

Between scan/add passes, scratch uses:

```text
ShaderWrite -> ShaderRead | ShaderWrite
```

Published outputs use:

```text
ShaderWrite -> ShaderRead
```

Segmented reduction publishes `SegmentSums`, `SegmentCounts`, and
`SegmentMeans` with the same `ShaderWrite -> ShaderRead` barrier.

Count readback publication uses:

```text
OutputCount: ShaderRead -> TransferRead -> ShaderRead
ReadbackCount: TransferWrite -> HostRead
```

Dispatch-args publication uses:

```text
DispatchArgs: ShaderWrite -> IndirectRead
```

The plan and publication helpers record these barriers as `RHI::MemoryAccess`
values. The opt-in Vulkan smoke compares scan, compaction, and segmented
reduction results with the CPU reference, verifies readback count and
dispatch-args publication, verifies scan overflow saturation on in-workgroup and
multiblock fixtures, and repeats the same compaction and segmented reduction
inputs to pin deterministic output/count behavior.
