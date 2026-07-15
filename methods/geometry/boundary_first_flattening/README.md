# Boundary First Flattening

Method ID: `geometry.boundary_first_flattening`. Status: **reference**.

This package records the paper intake and bounded CPU-reference contract for
Sawhney and Crane's Boundary First Flattening (BFF). The executable backend is
the `Geometry.Parameterization.Bff` module owned by `METHOD-023`.

## Scope and backend status

| Backend | Status | Owning task |
| --- | --- | --- |
| `cpu_reference` | reference | `METHOD-023` |
| optimized CPU / GPU | none planned | no follow-up owed without measured need |

The repository contract is intentionally narrower than the full paper:

- the input is an already-cut, connected manifold triangle mesh with disk
  topology;
- `AutomaticConformal` constructs compatible free-boundary data without a
  caller-provided target array;
- `TargetLengths` accepts one positive finite target length per boundary edge
  in UV units and reports the residual because boundary closure makes the
  targets approximate rather than an exact guarantee;
- `TargetAngles` accepts one finite exterior turning angle per boundary
  vertex in radians and requires a total of `2*pi` within the documented
  tolerance;
- cone singularities, cone placement, seam generation, and surface cutting are
  out of scope.
- the objective is conformal; it does not promise area preservation.

Invalid topology, malformed target arrays, non-finite values, inconsistent
exterior angles, singular systems, or unusable output diagnostics fail closed
without a UV payload. The direct result carries detailed `BffStatus` values;
successful results carry the shared `ParameterizationDiagnostics` quality
record plus BFF-specific boundary and closure residuals.

## Runtime ownership

The geometry method remains CPU-only and owns no config, ECS, runtime, or UI
state. `RUNTIME-176` exposes it through
`EngineConfig.sandbox.parameterization` with strategy token `bff`, mode tokens
`automatic_conformal`, `target_lengths`, and `target_angles`, plus the typed
`boundary_data`, `angle_sum_tolerance`, and `degeneracy_tolerance` values. The
configured runtime command converts that validated state to `BffParams`, writes
selected-mesh `v:texcoord`, marks the mesh dirty, and records undo/redo. Its
pointer-free UV view model carries UVs, triangle index triples, finite bounds,
and aggregate diagnostics; it does not invent cones, chart/seam records, or
per-face distortion. The active
[`UI-036`](../../../tasks/active/UI-036-sandbox-parameterization-editor-and-uv-split-view.md)
panel exposes BFF at `Mesh > Processing > Parameterize (UV)` alongside LSCM,
harmonic cotangent, and uniform Tutte. BFF mode, boundary data, and tolerances
stay in an explicit draft until the validated runtime config path accepts them;
execution then writes undoable `v:texcoord` values. The resizable UV pane draws
the returned layout over optional unit-square grid/checker backgrounds with
fit, zoom, and pan, while the controls show aggregate last-run diagnostics.
The `v:texcoord` writeback is observed by an already-bound 3D material that
samples the UV property, but the panel does not create or bind a UV/checker
material itself. UI-036 still owns the visible `Operational` proof.

## Evidence policy

Analytic and fail-closed correctness coverage lives in
`tests/unit/geometry/Test.BoundaryFirstFlattening.cpp`. The manifest-backed
smoke is a deterministic correctness and runtime-budget check;
it does not make a speedup, adoption, or paper-parity performance claim.

## References

- Rohan Sawhney and Keenan Crane. "Boundary First Flattening."
  *ACM Transactions on Graphics* 37(1), Article 5, 2017.
  <https://doi.org/10.1145/3132705>
- Paper intake: [`paper.md`](paper.md)
