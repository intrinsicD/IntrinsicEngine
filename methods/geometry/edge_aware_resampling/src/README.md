# Source

The serial CPU reference lives in the shared geometry implementation units.
Normal refinement and insertion helpers remain file-local; normal estimation,
radial/directional weights, density compensation, spatial queries, and
repulsion reuse their existing geometry owners.
