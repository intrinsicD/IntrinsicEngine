# Point construction

Point-anchored Hoppe-style reconstruction and kNN graph construction share
borrowed neighbor rows and the runtime's spatial cache. The
[formulation](paper.md) defines the selected variants; the
[runtime guide](../../../docs/architecture/point-construction.md) covers
canonical inputs, config, publication and diagnostics. Numerical and backend
limits are declared in [method.yaml](method.yaml).
