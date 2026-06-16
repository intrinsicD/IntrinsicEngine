# Constraints

## K01: Immediate Geometry Visibility Versus UV-Dependent Texture Binding
- **Constraint**: Meshes may render immediately with default material values, but UV-dependent texture bakes and texture bindings require valid mesh texture coordinates. Missing UVs schedule atlas generation and do not block first geometry visibility.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/done/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O02
