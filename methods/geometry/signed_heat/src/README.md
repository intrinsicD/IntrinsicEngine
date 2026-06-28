# Source Surface

The CPU reference backend is implemented in
`src/geometry/Geometry.HalfedgeMesh.SignedHeatMethod.cpp` so it can reuse the
geometry layer's DEC and sparse-solver modules without adding a new method
library target.
