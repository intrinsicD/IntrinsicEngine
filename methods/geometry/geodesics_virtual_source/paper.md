# Formulation and source intake

Trettner, Bommes, and Kobbelt, **Geodesic Distance Computation via Virtual
Source Propagation**, Computer Graphics Forum 40(5), 2021,
[DOI 10.1111/cgf.14371](https://doi.org/10.1111/cgf.14371).
The [authors' page](https://www.graphics.rwth-aachen.de/publication/03334/)
links the paper and supplemental `main.cc` reference implementation.

The selected formulation is the triangle-mesh, vertex-source CPU reference:
per-face virtual-source state (three squared vertex distances and an extra
path length), intrinsic triangle unfolding, the published 16-entry visibility
heuristic, and two FIFO queues with mean-edge-length normalization. It computes
an approximate distance field. The face-centroid winner rule discards competing
virtual sources; exact surface distances are not its contract.

Framework24's comparison source is
`experimental/framework24/lib_bcg_framework/src/bcg_mesh_geodesic_virtual_source_propagation.cpp`
in the IntrinsicEngine baseline revision `314db9ea5`. Its viewer entry is
`Mesh / Geodesics / Virtual Source Propagation`. The port keeps its geometric
formulation and publishes through IntrinsicEngine GeometrySources.

The new implementation normalizes initial centroid distances consistently,
keeps source-side flags with current halfedge state, seeds boundary vertices by
scanning live faces, resolves source ties deterministically, and keeps scratch
state outside mesh properties. Deleted and disconnected slots remain explicit;
there is no substitution of a largest finite distance for an unreachable slot.
The expansion budget returns an error with no output field. Heat geodesics
remain a separate existing API.

The authors' supplemental implementation and Framework24 both calculate
`next_h2`/`next_h3` side flags but enqueue `h2`/`h3`. This port retains the
flags in current halfedge state, avoiding both that omission and stale flags
when a queued face has since received a better source.

The relevant later implementation investigation is
[CGAL's 2023 GSP project report](https://github.com/CGAL/cgal/issues/7670).
It discusses barycenter sampling artifacts and target-directed early stopping.
Neither that early-stop variant nor the original paper's anisotropic,
point-cloud, line-source, parallel, or cache-reordered extensions is included.
They would require separate contracts and evidence.

## Numerical checks

The smoke fixture is an 8-by-8 unit grid with source vertex 0. It compares
vertex distances with the analytic planar distance, measures RMS and maximum
absolute error, and includes mesh construction outside the timed solve.
The declared bounds are RMS <= 0.05 position units and maximum absolute error
<= 0.075 position units. These are fixture acceptance criteria, not general error guarantees.

The separate interior-source grid fixture has a 0.25-unit absolute-error
acceptance bound. Boundary-source tests cap relative error at one percent on
their nonzero-distance vertices. Both are fixture criteria, not exactness or
general accuracy guarantees. Folded-vs-flat and scale checks use binary-exact
float coordinates to isolate intrinsic invariance from input quantization.
They do not establish continuity under small perturbations or canonical-dataset
parity with Framework24.

No timing result from the paper or CGAL is transferred to this implementation.
