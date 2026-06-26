# Progressive Poisson-Disk Sampling (method package)

Method ID: `geometry.progressive_poisson`. Status: **proposed** (intake scaffold;
no executable backend yet).

Computes a progressive ordering of an accepted subset of an input point set such
that every prefix `[0,k)` is a Poisson-disk (blue-noise) sampling at its hierarchy
level — instant level-of-detail via a single index cutoff.

## Scope and backend status

| Backend | Status | Owning task |
| --- | --- | --- |
| `cpu_reference` | planned (canonical truth) | METHOD-012 |
| `gpu_vulkan_compute` | planned (parity to reference) | METHOD-013 |

This directory currently holds the **paper intake** (`paper.md`), the **manifest**
(`method.yaml`), and placeholder package structure. The CPU reference
implementation, correctness tests, and smoke benchmark land with METHOD-012; the
`method.yaml` `status` flips to `reference` and `correctness_tests`/`benchmarks`
point at real files at that point.

## Enabling work (engine backlog)

- **GEOM-035** — triangle-mesh → point-cloud surface sampling (mesh input path).
- **GEOM-036** — blue-noise quality metrics (RDF/RAPS/periodogram/NN-CV/min-dist).
- **GRAPHICS-108** — reusable Vulkan compute scan + stream-compaction primitives.
- **RUNTIME-133** — figure/data export (CSV/JSON) for reproducible plots.
- **GRAPHICS-109** — offscreen frame capture to PNG for rendered figures.
- **RUNTIME-134** — interactive Sandbox playground binding every `SamplerConfig` knob.

## Known limitations

See `method.yaml` `known_limitations` and `paper.md` "Degenerate/edge cases".
Headlines: subsampling not generation (quality bounded by input density);
introduction-level splat-radius semantics (conservative for finer prefixes);
2D-only spectral metrics; degenerate inputs fail closed.

## References

- Sibling repository `code/progressive_poisson.{h,cu}`, `paper.tex`, `FIGURES.md`,
  `OPEN_DECISIONS.md`.
- `docs/agent/method-workflow.md` and `AGENTS.md` §6 (method implementation protocol).
