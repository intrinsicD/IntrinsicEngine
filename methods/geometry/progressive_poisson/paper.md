# Paper Intake — Progressive Poisson-Disk Sampling via Phase-Parallel Spatial Hashing

## Citation

- **Title:** GPU-Accelerated Progressive Poisson Disk Sampling via Phase-Parallel Spatial Hashing
- **Authors:** Unpublished working draft (intrinsicD); author list finalized at submission
- **Venue / Year:** In preparation, 2026
- **DOI:** n/a — unpublished working draft
- **URL:** https://github.com/intrinsicD/GPU-Accelerated-Progressive-Poisson-Disk-Sampling-via-Phase-Parallel-Spatial-Hashing

Reference implementation under intake: the sibling repository's `code/`
(`progressive_poisson.h`, `progressive_poisson.cu`) plus `paper.tex`,
`FIGURES.md`, and `OPEN_DECISIONS.md`.

## Core claim

Given an unordered set of `N` input points in `R^d` (primary focus `d in {2,3}`),
compute a progressive ordering of an accepted subset `M <= N` such that **every
prefix `[0,k)` (any `k <= M`) is a Poisson-disk (blue-noise) sampling at the
radius of its hierarchy level**. Level-of-detail then reduces to a single index
cutoff — `draw(0, k)` — with no octree traversal or LOD data structures.

## Mathematical formulation

- **Hierarchy of radii.** Level `L` has Poisson radius `r_L = base_radius / 2^L`;
  `base_radius` follows from `grid_width` at level 0. Each level refines the
  previous by halving the spacing (×4 candidate cells in 2D, ×8 in 3D).
- **Radius/cell ratio.** `r_L = radius_alpha * e_L` where `e_L` is the level cell
  edge; `radius_alpha` defaults to `sqrt(d)/2` for `d in {2,3}` (any value outside
  `(0,1)` selects the default).
- **Phase-parallel acceptance.** Within a level, grid cells are colored by per-axis
  parity — a `2^d`-coloring (4 phases in 2D, 8 in 3D). Phases are processed in
  sequence; **within** a phase all candidate points are tested and accepted in
  parallel, with no synchronization beyond a per-cell atomic claim. Two hash tables
  cooperate: one for conflict detection against all previously accepted points, one
  for per-level cell occupancy, jointly enforcing the minimum-distance constraint.
- **Guarantee (Theorem 1 in the draft).** For every prefix ending at a level
  boundary, the measured minimum pairwise distance satisfies `min_dist >= r_L`
  (Poisson-disk ratio `>= 1`). Any subset of a level prefix preserves the same
  minimum-distance bound, so within-level shuffling cannot violate the guarantee.
- **Splat radius.** Each accepted point stores its nearest-neighbor distance within
  the prefix ending at its own introduction level (`OPEN_DECISIONS` OD1, conservative
  introduction-level semantics).

## Inputs and outputs

- **Inputs:** SoA positions `points_x, points_y[, points_z]` (`N` each; `z` may be
  null for `d=2`); `SamplerConfig { dimension, grid_width, max_levels,
  hash_load_factor, radius_alpha, randomize_grid_origin, grid_origin_seed,
  shuffle_within_levels, shuffle_seed }`.
- **Outputs:** `ProgressivePoissonResult { order, level_offsets, splat_radii,
  base_radius }` plus reference diagnostics (accepted count, per-level counts,
  measured per-level minimum distance).

## Degenerate/edge cases

- Empty or single-point input → empty/trivial ordering with explicit diagnostic.
- Coincident/duplicate points → at most one survives per cell at the finest level;
  no infinite loop, no NaN radii.
- Non-finite coordinates → reject with an explicit diagnostic (no silent skip).
- `dimension` not in `{2,3}` → reject; `radius_alpha` outside `(0,1)` → fall back to
  `sqrt(d)/2`.
- Structured inputs (regular grids, scan lines) → `randomize_grid_origin` per level
  mitigates alignment artifacts; `shuffle_within_levels` removes intra-level phase
  banding for mid-level prefixes.
- Partial finest level → introduction-level splat radii under-estimate spacing;
  document, do not "fix" in the reference (parity target for METHOD-013).

## Implementation notes

- **Engine layering.** Hermetic method package: import only public method APIs and
  declared geometry types; no ECS/runtime/graphics/platform/app. The CPU reference is
  pure free functions over SoA spans (mirrors `methods/physics/particle_spring_reference`).
- **Reference first.** METHOD-012 implements the serial CPU reference and is the
  canonical truth; the CUDA `.cu` is the parallel form, not the contract. Reproduce
  accept/order semantics serially, then METHOD-013 ports the phase-parallel hashing
  to Vulkan compute (GRAPHICS-108 scan/compaction) and proves parity within tolerance.
- **Determinism.** Fixed `(points, config, seeds)` must reproduce identical `order`
  and `level_offsets`. Per-level grid-origin offsets derive deterministically from
  `grid_origin_seed`; within-level permutation from `shuffle_seed`.
- **Inputs from meshes.** Triangle meshes are sampled to a dense surface cloud first
  via GEOM-035 (area-weighted barycentric sampling), then fed to the sampler.
- **Evaluation.** Quality metrics (RDF, RAPS, periodogram, NN-distance CV,
  min-distance ratio, coverage) come from GEOM-036; figure/data export from
  RUNTIME-133; interactive knob control from RUNTIME-134. See the sibling `FIGURES.md`
  for the exact figure specs and metric definitions.
