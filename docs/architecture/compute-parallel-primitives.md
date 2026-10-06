# Compute Parallel Primitives

Status: canonical for the `GRAPHICS-108` scan/compaction seam, the
`GRAPHICS-111` segmented float reduction seam, and the `GRAPHICS-112`
work-efficient scan/overflow guard.

`Extrinsic.Graphics.ComputeParallelPrimitives` owns generic `uint32` prefix-scan,
stream-compaction, count-publication, and deterministic float segmented-reduction
building blocks for GPU-oriented methods. The seam lives in `graphics` and
imports RHI contracts and Core helpers only (for example the shared
`Core::CeilDiv` from `Extrinsic.Core.IntegerMath`, imported privately); it must not import ECS, runtime, platform, app,
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

GRAPHICS-108/111/112 pin five shader assets under `assets/shaders/`, and
GRAPHICS-148 adds `parallel_radix_histogram.comp` and
`parallel_radix_scatter.comp` (shared layout in `parallel_radix_common.glslinc`):

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

Radix sort scratch (GRAPHICS-148) holds, in order, a copy of the `N` records
(`N * (KeyWords + 1) * sizeof(uint32)` bytes) for ping-pong, the digit-major
counts (`16 * ceil(N / 256)` `uint32`), their exclusive scan
(`PrefixOffsetsOffsetBytes`), and that scan's recursive levels.

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

Radix sort planning (GRAPHICS-148) emits, per 4-bit digit pass, one
`RadixHistogram` (per 256-record tile, digit-major counts), the exclusive scan
of those counts, and one `RadixScatter`, which sorts the tile by digit with
four stable one-bit splits in shared memory and writes each record to its
digit's scanned offset plus its rank among equal digits in the tile. Records
are `KeyWords` key words (1 or 2, least significant first) and one payload
word; passes cover `KeyBits` rounded up to an even pass count, alternating
between the caller's records (`Keys` role, at `ElementsOffsetBytes`) and the
scratch copy, so the result ends in place. Key bits at or above `KeyBits` read
as zero, so a partial last digit and the padding pass keep equal requested keys
in order. The sort is stable; its own passes use no subgroup operations and
only integer shared-memory atomics for the counts, while the digit-count scan
is `parallel_prefix_scan.comp`, which needs subgroup arithmetic (so does every
consumer, now including `Extrinsic.Graphics.PointLBVH`). Inputs are limited to
65535 tiles of 256 records, the guaranteed Vulkan workgroup count per dispatch.
It is checked against `SortRecordsByKeyCpu` (a `std::stable_sort` by the low
`KeyBits`) in the opt-in Vulkan smoke, three runs per case, including keys with
bits above `KeyBits`. `Extrinsic.Graphics.PointLBVH` sorts its (Morton code,
index) records with it (30 key bits, eight passes).

`CreateComputePipeline` creates one compute pipeline from a shader path relative to the
shader root; the compute workspaces of the graphics layer (property filter, sparse CG,
keypoints, CPD E-step, point LBVH, farthest-point sampling) use it.
`CreateParallelPrimitivePipelines` creates (with resolved shader paths) the
pipelines a list of primitive kinds records with, and
`DestroyParallelPrimitivePipelines` releases them; consumers use these instead
of creating the pipeline set by hand.

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

## Device Capabilities And Exact-Arithmetic Policy

`RHI::IDevice` reports the optional compute capabilities kernels may depend on
(GRAPHICS-149). Defaults are the conservative answers every kernel may assume;
the Vulkan device fills them from the physical device:

| Query | Default | Vulkan source |
| --- | --- | --- |
| `SupportsShaderFloat64()` | false | `shaderFloat64` (enabled when present) |
| `SupportsShaderInt64Atomics()` | false | `shaderBufferInt64Atomics` (Vulkan 1.2, enabled when present) |
| `SupportsSubgroupArithmetic()` | false | subgroup properties: compute stage and arithmetic operations |
| `SubgroupSize()` | 0 (unknown) | `subgroupSize` |
| `MaxComputeSharedMemoryBytes()` | 16384 | `maxComputeSharedMemorySize` |

A kernel that needs an optional capability refuses unsupported devices with a
diagnostic and leaves the work to its CPU reference; it never assumes a subgroup
width (subgroup paths must give the same result as a workgroup-shared fallback)
and sizes shared memory from the query.

Kernels that must match a CPU reference bit for bit (the METHOD-055 hole sieve,
the METHOD-056 double accumulations, the METHOD-014 conflict checks) follow these
rules:

- Mark every floating-point operation of the compared arithmetic `precise`
  (SPIR-V `NoContraction`), matching the CPU references' `fp contract(off)`;
  keep the reference's operation order (for distances: `dx*dx`, then `+ dy*dy`,
  then `+ dz*dz`).
- No floating-point atomics. Reductions run in a fixed order: per-workgroup
  partials in shared memory with a fixed tree, then a second dispatch (or the
  CPU) reducing the partials in index order.
- GLSL has no double-precision `exp`; kernels needing one reuse the bounded
  `ExpNonPositive` of `property_filter.comp`.
- Long-running work is split into bounded dispatches (no persistent kernels), so
  a driver watchdog cannot abort it; state lives in buffers between dispatches.
- One-ulp bound adjustments (`nextafter`) are done with 64-bit integer bit
  operations on the double's representation.
