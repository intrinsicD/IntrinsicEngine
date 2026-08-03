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
| `gpu_vulkan_compute` | recordable Vulkan dispatch + operational shared result-transport/parser seam; CPU fallback until compute parity lands | METHOD-013 / METHOD-014 |

This directory holds the **paper intake** (`paper.md`), the **manifest**
(`method.yaml`), and the METHOD-012 CPU reference implementation under
`include/` and `src/`. The reference backend is the canonical truth for
correctness tests and smoke benchmarks. METHOD-013 owns the runtime/config
backend selection contract, CPU fallback diagnostics, the Vulkan shader/layout
planning and recording seams, parsed GPU readback payloads, and the future
operational Vulkan parity reporting slice. RUNTIME-195 replaces the three
dedicated readback targets and blocking result reads with one shared async
three-range transfer batch; METHOD-014 retains compute execution and public
CPU/GPU parity.

## Related engine work

- **GEOM-035** — explicit triangle-mesh surface-candidate generation for
  experiments that need a new point-cloud dataset; it is not an implicit input
  path for this finite-set method.
- **GEOM-036** — blue-noise quality metrics (RDF/RAPS/periodogram/NN-CV/min-dist).
- **GRAPHICS-108** — reusable Vulkan compute scan + stream-compaction primitives.
- **RUNTIME-133** — figure/data export (CSV/JSON) for reproducible plots.
- **GRAPHICS-109** — offscreen frame capture to PNG for rendered figures.
- **RUNTIME-134** — interactive Sandbox playground binding every `SamplerConfig` knob.
- **RUNTIME-136** — Sandbox CPU/GPU backend selector and requested-vs-actual
  backend readout.

## Interactive usage

The runtime command accepts the existing `Vertices` component of mesh, graph,
and point-cloud entities through one typed operation. The Sandbox exposes that
same operation under Mesh, Graph, and PointCloud Processing, with copied
readiness explaining missing, wrongly typed, empty, or non-finite
`v:position` data. No lane surface-samples, reorders, replaces, or changes the
provenance of the source component. The command forwards every
reference `Config` knob
(`dimension`, `grid_width`, `max_levels`, `hash_load_factor`, `radius_alpha`,
`randomize_grid_origin`, `grid_origin_seed`, `shuffle_within_levels`,
`shuffle_seed`) plus the backend request (`cpu_reference` or
`gpu_vulkan_compute`) through a typed command DTO and the engine config-control
field `sandbox.progressive_poisson`, and publishes source-cardinality vertex
float properties for visualization:

- `v:poisson_level`
- `v:poisson_rank`
- `v:poisson_splat_radius`
- `v:poisson_prefix_visible`

The prefix property uses `0` as hidden and `1` as visible; a requested prefix
count of `0` means all accepted points. Accepted inputs store zero-based global
progressive rank, accepted hierarchy level, introduction-level splat radius,
and prefix visibility. Inputs outside the accepted subset retain deterministic
sentinels: rank `-1`, level `-1`, radius `0`, and visibility `0`. The runtime
result carries requested backend id, actual backend id, CPU fallback reason when
present, and accepted-point counts per progressive level for the Sandbox
readout. As of METHOD-013 Slice D.1,
requesting `gpu_vulkan_compute` builds against a runtime recordable dispatch
contract (`Runtime.ProgressivePoissonGpuBackend`) that pins storage-buffer
layout, BDA push/state records, shader asset paths, per-level build/accept
dispatches, accepted/remaining GRAPHICS-108 stream-compaction delegation,
runtime-owned SoA position uploads, and production result buffers for
`order`/`level_offsets`/`splat_radii`. RUNTIME-195 drains those three ranges as
one copied `Graphics.GpuTransfer` batch and leaves structural parsing in the
method adapter; it allocates no duplicate host-visible readback buffers and
uses no blocking result `IDevice::ReadBuffer`. An actual sanitizer-enabled
Vulkan smoke seeds a CPU-reference-shaped payload into the production-shaped
buffers and proves the transfer/parser seam. It is not compute parity: public
Sandbox execution still returns the CPU reference fallback until METHOD-014
proves the Vulkan compute output path against the CPU reference.

The panel deliberately uses *existing input vertices*, *accepted subset*,
*hierarchy level*, and *prefix* terminology. That follows the finite-candidate
subset and progressive-prefix framing in Yuksel, Dieckmann--Klein, Brandt et
al., and Christensen et al.; Brandt et al.'s surface-candidate rasterization is
an upstream dataset-construction stage, not a panel-side conversion rule.

Widget edits preview and hot-apply a serialized `EngineConfig` through
`Engine::PreviewEngineConfigControlDocument` and
`Engine::ApplyEngineConfigHotSubset`; when `auto_run_on_edit` is enabled, the
Sandbox schedules a debounced rerun. The explicit Run action uses the same
config path before invoking the command. The Sandbox backend selector writes
that same config field, and the result readout shows requested backend, actual
backend, and CPU fallback reason when present. METHOD-013 owns the backend
command/config seam, planning-only Vulkan-compute seam, executable backend, and
CPU/GPU parity lineage; METHOD-014 owns the remaining operational compute and
public parity work.

## Known limitations

See `method.yaml` `known_limitations` and `paper.md` "Degenerate/edge cases".
Headlines: subsampling not generation (quality bounded by input density);
introduction-level splat-radius semantics (conservative for finer prefixes);
2D-only spectral metrics; degenerate inputs fail closed.

## References

- Sibling repository `code/progressive_poisson.{h,cu}`, `paper.tex`, `FIGURES.md`,
  `OPEN_DECISIONS.md`.
- Brandt et al., *Visibility-Aware Progressive Farthest Point Sampling on the GPU*, CGF
  2019, DOI `10.1111/cgf.13848`.
- Yuksel, *Sample Elimination for Generating Poisson Disk Sample Sets*, CGF
  2015, DOI `10.1111/cgf.12538`.
- Dieckmann and Klein, *Hierarchical Additive Poisson Disk Sampling*, VMV 2018,
  DOI `10.2312/vmv.20181256`.
- Christensen et al., *Progressive Multi-Jittered Sample Sequences*, EGSR 2018.
- `docs/agent/method-workflow.md` and `AGENTS.md` §6 (method implementation protocol).
