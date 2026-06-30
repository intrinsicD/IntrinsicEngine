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
- backend-neutral GPU dispatch planning and RHI command recording for Vulkan
  compute execution.

The GPU record API remains fail-closed on unsupported devices: non-operational
devices report `DeviceUnavailable`, missing caller-owned recorder dependencies
report `InvalidInput`, and invalid handles, BDAs, pipelines, or undersized
scratch buffers report `InvalidGpuResource`. Operational Vulkan paths record
commands through `RHI::ICommandContext`; no Vulkan-native type leaks through the
graphics API.

## Shader Assets

Slice B pins three shader assets under `assets/shaders/`:

- `parallel_prefix_scan.comp` performs one 256-lane workgroup-local scan and
  optionally writes one block sum per workgroup. For stream compaction, the
  recorder sets a mode bit that normalizes nonzero flags to `1` before scan so
  GPU compaction matches the CPU reference's "nonzero means keep" contract.
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

Slice C turns that plan into RHI commands by binding one of three caller-provided
compute pipelines, pushing the matching scalar BDA push-constant block, and
dispatching the planned group count. Scratch is either caller-provided or
allocated as an owned `RHI::BufferManager::BufferLease` returned in the record
result so its lifetime spans command execution.

Between scan/add passes, scratch uses:

```text
ShaderWrite -> ShaderRead | ShaderWrite
```

Published outputs use:

```text
ShaderWrite -> ShaderRead
```

The plan records these barriers as `RHI::MemoryAccess` values. Slice C owns
the Vulkan parity smoke for scan and compaction. Slice D owns higher-level
readback and/or indirect-args integration for the compacted count before task
retirement.
