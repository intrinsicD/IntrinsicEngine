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
state. `RUNTIME-176` owns stable strategy-token serialization, conversion to
the typed BFF payload, selected-mesh `v:texcoord` writeback, dirty marking,
undo, and the pointer-free UV view model. UI presentation remains downstream
of that runtime facade.

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
