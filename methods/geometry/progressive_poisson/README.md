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
| `gpu_vulkan_compute` | planned (parity to reference) | METHOD-013 |

This directory holds the **paper intake** (`paper.md`), the **manifest**
(`method.yaml`), and the METHOD-012 CPU reference implementation under
`include/` and `src/`. The reference backend is the canonical truth for
correctness tests and smoke benchmarks; METHOD-013 owns the future Vulkan compute
backend and parity reporting.

## Enabling work (engine backlog)

- **GEOM-035** — triangle-mesh → point-cloud surface sampling (mesh input path).
- **GEOM-036** — blue-noise quality metrics (RDF/RAPS/periodogram/NN-CV/min-dist).
- **GRAPHICS-108** — reusable Vulkan compute scan + stream-compaction primitives.
- **RUNTIME-133** — figure/data export (CSV/JSON) for reproducible plots.
- **GRAPHICS-109** — offscreen frame capture to PNG for rendered figures.
- **RUNTIME-134** — interactive Sandbox playground binding every `SamplerConfig` knob.

## Interactive usage

`RUNTIME-134` Slice A exposes the CPU reference backend in the Sandbox
PointCloud processing window. The runtime command validates the selected
point-cloud `GeometrySources`, forwards every reference `Config` knob
(`dimension`, `grid_width`, `max_levels`, `hash_load_factor`, `radius_alpha`,
`randomize_grid_origin`, `grid_origin_seed`, `shuffle_within_levels`,
`shuffle_seed`) through a typed command DTO, and publishes per-point float
properties for visualization:

- `p:poisson_level`
- `p:poisson_phase`
- `p:poisson_splat_radius`
- `p:poisson_prefix_visible`

The prefix property uses `0` as hidden and `1` as visible; a requested prefix
count of `0` means all accepted points. `p:poisson_phase` is a deterministic
display bucket derived from level-local rank modulo the 2D/3D phase count because
the CPU reference result does not expose its internal conflict-resolution phase
as a stable public output yet.

## Known limitations

See `method.yaml` `known_limitations` and `paper.md` "Degenerate/edge cases".
Headlines: subsampling not generation (quality bounded by input density);
introduction-level splat-radius semantics (conservative for finer prefixes);
2D-only spectral metrics; degenerate inputs fail closed.

## References

- Sibling repository `code/progressive_poisson.{h,cu}`, `paper.tex`, `FIGURES.md`,
  `OPEN_DECISIONS.md`.
- `docs/agent/method-workflow.md` and `AGENTS.md` §6 (method implementation protocol).
