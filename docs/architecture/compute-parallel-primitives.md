# Compute Parallel Primitives

Status: canonical for the `GRAPHICS-108` scan/compaction seam.

`Extrinsic.Graphics.ComputeParallelPrimitives` owns generic `uint32` prefix-scan
and stream-compaction building blocks for GPU-oriented methods. The seam lives
in `graphics` and imports RHI contracts only; it must not import ECS, runtime,
platform, app, method packages, live asset services, or Vulkan-native handles.

## Current Contract

The default CPU-supported gate owns two contracts:

- deterministic CPU reference helpers for exclusive/inclusive prefix scan and
  stable compaction by flags;
- backend-neutral GPU dispatch planning for the future Vulkan implementation.

The GPU record facade remains fail-closed until the Vulkan execution slice lands:
non-operational devices report `DeviceUnavailable`, invalid handles report
`InvalidGpuResource`, and operational devices with valid resources report
`UnsupportedInCurrentSlice`.

## Shader Assets

Slice B pins three shader assets under `assets/shaders/`:

- `parallel_prefix_scan.comp` performs one 256-lane workgroup-local scan and
  optionally writes one block sum per workgroup.
- `parallel_scan_add_offsets.comp` adds recursively scanned block offsets back
  into an existing scan output.
- `parallel_compact_by_flags.comp` scatters kept keys using exclusive prefix
  offsets and writes the compacted count.

All three use the promoted Buffer Device Address convention: storage buffers are
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

## Dispatch And Barriers

Prefix scan planning emits:

1. one `PrefixBlockScan` over the source values;
2. zero or more recursive `PrefixBlockScan` passes over scratch block sums;
3. top-down `PrefixAddBlockOffsets` passes;
4. a final `Output` publication barrier.

Stream compaction planning emits the same exclusive scan sequence over `Flags`,
then one `StreamCompactScatter` pass.

Between scan/add passes, scratch uses:

```text
ShaderWrite -> ShaderRead | ShaderWrite
```

Published outputs use:

```text
ShaderWrite -> ShaderRead
```

The plan records these barriers as `RHI::MemoryAccess` values. Slice C owns
turning the plan into real Vulkan command recording and opt-in `gpu;vulkan`
parity tests.
