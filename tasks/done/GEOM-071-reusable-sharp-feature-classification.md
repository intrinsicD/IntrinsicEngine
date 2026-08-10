---
id: GEOM-071
theme: I
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-10T15:31:17Z"
maturity_target: CPUContracted
completed_on: 2026-08-10
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.property-coherence]
contract_review: >-
  The task creates a reusable geometry-owned public classifier contract and
  materializes the canonical e:feature property, so task-contract discovery
  and property coherence apply. geometry.element-domain-sources does not apply:
  this slice has no ECS/runtime eligibility or publication path and operates on
  a caller-owned HalfedgeMesh plus borrowed slot-aligned normals.
---
# GEOM-071 — Reusable sharp-feature classification

## Status

- Completed and retired on 2026-08-10 at `CPUContracted` maturity.
- Implementation commit: `191861b24f25262aec5a0a067f3baea515ca235d`.
- The first independent pass requested explicit
  submesh-view rejection, scale-stable normalization at representable finite
  extremes, a real multi-collapse current-normal regression, diagnostic
  classification of the superseded policy receipt, and final-surface review.
  The revised 18-case direct/adoption selector and all 4,209 selected CPU tests
  are green; the tracked `GlfwLifecycleLsan` control self-skipped. Strict
  layering, test-layout, docs-link, task-policy/task-validation, root-hygiene,
  ARA, generated-inventory, and clean-workshop checks passed. The final
  retirement surface is bound for high-risk review acceptance.

## Goal
- Extract the proven FA-QEM boundary/dihedral feature logic into a small geometry-owned classifier and make Simplification its behavior-preserving first production adopter.

## Non-goals
- No detector interface, registry, strategy hierarchy, automatic global tagging subsystem, UI, or GPU path.
- No Catmull-Clark stencil changes; `GEOM-072` is the concrete second adopter.
- No change to FA-QEM's special boundary-turning promotion or collapse policy.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- `Geometry.HalfedgeMesh.Simplification.cpp` privately computes face normals, treats boundary/invalid edges conservatively, tests dihedral threshold, counts incident feature edges, and maps those counts into FA-QEM protection.
- Loop subdivision already consumes a canonical boolean edge-feature property, while Catmull-Clark lacks equivalent crease behavior. A focused plain-data/free-function classifier plus canonical property materialization prevents those algorithms from re-deriving subtly different geometry tests.
- A small `Geometry.HalfedgeMesh.Features` module is justified because the existing utility module is already broad; it must stay a data/function seam rather than grow into a framework.

## Right-sizing note
- Smallest useful element: one focused module containing finite params, slot-aligned result records, and free functions for classification, the evolving-edge predicate, and property materialization.
- Simpler alternative considered: leave the FA-QEM helpers private. It cannot satisfy the present `GEOM-072` consumer or establish one canonical `e:feature` publication path without duplicating the geometry test.
- Blast radius: `src/geometry` module registration, the private FA-QEM adopter, focused geometry tests, and the generated module inventory only; no runtime, UI, asset, or backend surface changes.
- Reintroduction trigger: an interface, registry, strategy object, or cached service is reconsidered only when a present caller needs runtime-selected behavior or owned incremental state. Neither need exists in this slice.

## Required changes
- [x] Add finite validated params (`BoundaryIsFeature`, dihedral threshold in `[0, 180]`) and a plain deterministic classification result indexed by existing edge/vertex slots.
- [x] Accept a borrowed, slot-aligned face-normal view so each caller retains its established face-normal policy. Simplification supplies its current leading-triangle compatibility normals (including recomputed surviving one-ring values); polygon callers may supply canonical polygon/Newell normals without silently changing Simplification behavior. Normalize every finite nonzero supplied normal inside the classifier before evaluating the dihedral predicate so classification is independent of normal magnitude.
- [x] Reject `Mesh` submesh views explicitly before slot iteration or property materialization; their handle-based connectivity addresses absolute backing-storage indices and is not compatible with this owning-slot result contract.
- [x] Preserve lossless vertex incidence categories: no incident feature edges, one endpoint edge, exactly two crease edges, and three-or-more junction/corner edges. Consumers choose their own policy from those facts.
- [x] Classify live boundary edges and interior edges whose adjacent face normals exceed the threshold; handle deleted, dangling, degenerate, zero-normal, and non-finite geometry conservatively and explicitly. A live edge with an unusable adjacent normal is feature-classified (fail closed), never passed through an unnormalized dot-product comparison.
- [x] Expose one narrow shared sharp-edge predicate if Simplification's evolving-mesh collapse checks need it; do not replace those dynamic checks with a stale initial edge mask.
- [x] Replace Simplification's private initial classifier/count/kind logic with the shared result while preserving its current mapping (`1` remains unprotected, `2` is line, `3+` is corner).
- [x] Keep Simplification's boundary-turning promotion local because no second consumer needs it.
- [x] Add one narrow free function/overload that materializes a validated classification edge mask into a caller-named boolean edge property (canonical default `e:feature`) for existing Loop and dependent `GEOM-072` consumers.
- [x] Register any new module without adding an umbrella re-export unless the existing geometry module policy requires one.

## Tests
- [x] Cover planar smooth interior edges, configurable boundary classification, a folded crease, and a cube with exact expected edge/incidence counts.
- [x] Cover invalid threshold, deleted slots, dangling adjacency, zero-area faces, and non-finite geometry with the documented fail-closed result.
- [x] Cover supplied-normal size/non-finite failures and a quad/n-gon fixture using canonical polygon normals, while pinning Simplification's historical non-triangle normal behavior separately.
- [x] Scale otherwise identical adjacent normals independently and prove the dihedral result is angle-invariant; cover a zero normal and assert the documented fail-closed feature result.
- [x] Cover denormal and near-maximum finite normal magnitudes, nonzero-offset submesh views, and a disabled boundary policy with an unusable sole-face normal.
- [x] Assert slot alignment and deterministic repeated output.
- [x] Round-trip classification through the canonical edge property and assert exact edge-mask equality.
- [x] Pin FA-QEM candidate/protection behavior and feature diagnostic counts on existing simplification fixtures before and after adoption.
- [x] Include an evolving-mesh simplification regression proving collapse legality still uses current geometry where required.

## Docs
- [x] Document threshold convention, boundary policy, incidence categories, normal normalization/invalid-normal policy, and borrowed/indexed lifetime.
- [x] Regenerate `docs/api/generated/module_inventory.md` for the new module.
- [x] Update the geometry backlog notes and link the dependent `GEOM-072` consumer.

## Acceptance criteria
- [x] Simplification contains no duplicate batch feature-edge/count classifier and remains behavior-identical on its regression corpus.
- [x] The result preserves enough facts for FA-QEM and Catmull-Clark to apply different one-edge policies.
- [x] The new surface is plain params/result/free functions with no abstract detector machinery.
- [x] The default CPU geometry and layering gates pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^(HalfedgeMeshFeatures\.|Simplification\.(FeatureAware|NonTriangleFeatureNormals|EvolvingFeatureLegality))' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out /tmp/intrinsic-modules.md
diff -u docs/api/generated/module_inventory.md /tmp/intrinsic-modules.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Collapsing one-edge and three-or-more-edge vertices into an indistinguishable category.
- Replacing dynamic FA-QEM legality checks with stale pre-collapse data.
- Adding a general feature-detection framework or unrelated mesh analysis.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
