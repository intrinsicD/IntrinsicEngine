---
id: GEOM-072
theme: I
depends_on: [GEOM-071]
maturity_target: CPUContracted
---
# GEOM-072 — Catmull-Clark crease masks

## Goal
- Add opt-in sharp-feature stencils and multi-iteration crease-tag propagation to Catmull-Clark subdivision through the same canonical boolean edge-feature property used by Loop.

## Non-goals
- No semi-sharp/fractional crease weights, OpenSubdiv compatibility layer, new tagging subsystem, Loop rewrite, UI, or GPU implementation.
- No automatic behavior change for existing untagged/default Catmull-Clark calls.
- No subdivision-surface limit evaluator or adaptive tessellation.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- `Geometry.CatmullClark` currently smooths every interior edge and vertex. `Geometry.Subdivision` already establishes the boolean feature-edge midpoint, crease-vertex, corner, and split-tag conventions for Loop meshes.
- `Engine25/Core/Frameworks/Geometry/MeshSubdivision.cpp::CatmullClark` is the cleanest old-engine source for the midpoint/crease/corner and split-tag rules (cross-checked against `bcg_geometry_processing/src/MeshSubdivision.cpp` and Engine24's PMP copy), but the existing Loop property semantics are the sole Catmull-Clark input contract. `GEOM-071` materializes detected facts into that property before subdivision; Catmull-Clark does not gain a second direct mask input.
- Catmull-Clark interprets feature incidence differently from FA-QEM: exactly two incident crease edges use the crease stencil; one-edge endpoints and three-or-more-edge junctions remain fixed.

## Required changes
- [ ] Mirror Loop's default-off `PreserveFeatureEdges` plus `FeatureEdgePropertyName` parameters; the named canonical boolean edge property is the only feature input.
- [ ] Validate the enabled property for exact edge-slot coverage and live-edge consistency before clearing or mutating the output mesh.
- [ ] Use endpoint midpoint positions for tagged edge points.
- [ ] Use the `1/8, 3/4, 1/8` crease stencil for old vertices with exactly two tagged incident edges; keep one-edge endpoints and three-or-more-edge junction/corner vertices fixed.
- [ ] Propagate each split crease into the two corresponding output edge tags on every iteration so later iterations use the same feature skeleton.
- [ ] Apply matching feature-aware interpolation to `v:texcoord` while preserving current untagged texture-coordinate behavior.
- [ ] Keep property naming aligned with Loop subdivision rather than creating a Catmull-specific tag vocabulary.

## Tests
- [ ] Classify a cube with `GEOM-071`, materialize the canonical property, and assert its twelve crease edges/eight corners remain sharp after subdivision.
- [ ] Cover an open crease chain, its endpoints, an exactly-two-edge crease vertex, and a three-or-more-edge junction with analytic stencil expectations.
- [ ] Run multiple iterations and assert split-tag count/connectivity propagation at each level.
- [ ] Assert feature-aware texture-coordinate interpolation and finite output.
- [ ] Reject missing/misaligned enabled feature data without clearing an existing output mesh.
- [ ] Prove default-off/unfeatured output is exactly equal to the current Catmull-Clark regression fixtures.

## Docs
- [ ] Document canonical-property ownership/lifetime, incidence rules, tag propagation, UV semantics, and default-off compatibility in `Geometry.CatmullClark`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the public module surface changes.
- [ ] Update the geometry backlog dependency notes.

## Acceptance criteria
- [ ] Tagged creases survive every requested iteration with correct endpoint/junction behavior.
- [ ] Default calls remain exact current behavior.
- [ ] Malformed opted-in input fails before output mutation.
- [ ] A classifier-to-canonical-property-to-Catmull integration test consumes `GEOM-071` facts without adding dihedral logic or a second feature-input path to Catmull-Clark.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Catmull|Feature' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Applying feature rules when the option is disabled.
- Dropping crease tags after the first iteration.
- Adding semi-sharp weights or another feature-property naming scheme.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
