# Progressive Poisson-Disk Sampling (method package)

Method ID: `geometry.progressive_poisson`. Status: **reference** (CPU
reference backend available).

Computes a progressive ordering of an accepted subset of an input point set such
that every prefix `[0,k)` is a Poisson-disk (blue-noise) sampling at its hierarchy
level — instant level-of-detail via a single index cutoff.

## Scope and backend status

| Backend | Status | Owning task |
| --- | --- | --- |
| `cpu_reference` | reference (canonical truth) | METHOD-012 |
| `gpu_vulkan_compute` | recordable Vulkan dispatch + upload/readback-copy seam; CPU fallback until parity slices land | METHOD-013 |

This directory holds the **paper intake** (`paper.md`), the **manifest**
(`method.yaml`), and the METHOD-012 CPU reference implementation under
`include/` and `src/`. The reference backend is the canonical truth for
correctness tests and smoke benchmarks. METHOD-013 owns the runtime/config
backend selection contract, CPU fallback diagnostics, the Vulkan shader/layout
planning and recording seams, and the future Vulkan upload/readback/parity
reporting slices.

## Enabling work (engine backlog)

- **GEOM-035** — triangle-mesh → point-cloud surface sampling (mesh input path).
- **GEOM-036** — blue-noise quality metrics (RDF/RAPS/periodogram/NN-CV/min-dist).
- **GRAPHICS-108** — reusable Vulkan compute scan + stream-compaction primitives.
- **RUNTIME-133** — figure/data export (CSV/JSON) for reproducible plots.
- **GRAPHICS-109** — offscreen frame capture to PNG for rendered figures.
- **RUNTIME-134** — interactive Sandbox playground binding every `SamplerConfig` knob.
- **RUNTIME-136** — future Sandbox CPU/GPU backend toggle after METHOD-013.

## Interactive usage

`RUNTIME-134` Slices A-D.1 expose the CPU reference backend in the Sandbox
PointCloud and Mesh processing windows. The runtime command validates selected
point-cloud `GeometrySources`, or samples a selected editable mesh surface to a
point cloud through `GEOM-035` (`Geometry.PointCloud.SurfaceSampling`), forwards
every reference `Config` knob
(`dimension`, `grid_width`, `max_levels`, `hash_load_factor`, `radius_alpha`,
`randomize_grid_origin`, `grid_origin_seed`, `shuffle_within_levels`,
`shuffle_seed`) plus the backend request (`cpu_reference` or
`gpu_vulkan_compute`) through a typed command DTO and the engine config-control
field `sandbox.progressive_poisson`, and publishes per-point float properties for
visualization:

- `p:poisson_level`
- `p:poisson_phase`
- `p:poisson_splat_radius`
- `p:poisson_prefix_visible`

The prefix property uses `0` as hidden and `1` as visible; a requested prefix
count of `0` means all accepted points. `p:poisson_phase` is a deterministic
display bucket derived from level-local rank modulo the 2D/3D phase count because
the CPU reference result does not expose its internal conflict-resolution phase
as a stable public output yet.

Mesh runs expose additional surface-sampling controls (`sample_count`, `seed`,
`min_triangle_area`, and `interpolate_vertex_normals`) and publish the sampled
cloud back onto the selected entity for point rendering. The runtime result
reports the written sample count, accepted triangle count, rejected face count,
and total sampled surface area. It also carries requested backend id, actual
backend id, CPU fallback reason when present, and accepted-point counts per
progressive level for the Sandbox readout. As of METHOD-013 Slice C.2,
requesting `gpu_vulkan_compute` builds against a runtime recordable dispatch
contract (`Runtime.ProgressivePoissonGpuBackend`) that pins storage-buffer
layout, BDA push/state records, shader asset paths, per-level build/accept
dispatches, accepted/remaining GRAPHICS-108 stream-compaction delegation,
runtime-owned SoA position uploads, and readback-copy targets for
`order`/`level_offsets`/`splat_radii`. Public Sandbox execution still returns
the CPU reference fallback until Vulkan output parsing and CPU/GPU parity land
in later METHOD-013 slices.

Widget edits preview and hot-apply a serialized `EngineConfig` through
`Engine::PreviewEngineConfigControlDocument` and
`Engine::ApplyEngineConfigHotSubset`; when `auto_run_on_edit` is enabled, the
Sandbox schedules a debounced rerun. The explicit Run action uses the same
config path before invoking the CPU reference command.
The visible backend toggle is deferred to RUNTIME-136; METHOD-013 owns the
backend command/config seam, planning-only Vulkan-compute seam, executable
backend, and CPU/GPU parity.

## Known limitations

See `method.yaml` `known_limitations` and `paper.md` "Degenerate/edge cases".
Headlines: subsampling not generation (quality bounded by input density);
introduction-level splat-radius semantics (conservative for finer prefixes);
2D-only spectral metrics; degenerate inputs fail closed.

## References

- Sibling repository `code/progressive_poisson.{h,cu}`, `paper.tex`, `FIGURES.md`,
  `OPEN_DECISIONS.md`.
- `docs/agent/method-workflow.md` and `AGENTS.md` §6 (method implementation protocol).
