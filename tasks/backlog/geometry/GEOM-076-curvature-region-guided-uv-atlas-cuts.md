---
id: GEOM-076
theme: I
depends_on: [METHOD-038, GEOM-057]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This follow-up consumes METHOD-038's accepted face/edge or continuous-boundary contract, may turn accepted boundaries into explicit UV-atlas chart constraints and corner-domain UV output, and must expose any adoption policy through the shared runtime/config/UI path. geometry.parameterization-optimization does not apply unless implementation changes the shared ARAP/SLIM optimization numerics."
---
# GEOM-076 — Evidence-gated curvature-region UV atlas cuts

## Goal
- Determine whether METHOD-038's independently accepted, feature-aligned curvature boundaries improve useful UV-atlas decomposition, and only after preregistered quality gates pass, integrate them as explicit chart/cut hints for parameterization and atlas generation.

## Non-goals
- No assumption that every curvature boundary is a good seam, no default-policy change before comparative evidence, and no atlas-quality claim from visual inspection alone.
- No destructive split of the authoritative ECS mesh. Accepted cuts remain chart/seam metadata and corner-domain `h:texcoord`; GPU vertex duplication stays an upload concern.
- No new clustering implementation, segmentation service, generic cut-policy framework, parameterization solver, packer, or backend abstraction.
- No reimplementation of shared parameterization optimization numerics. If a later solver needs those changes, declare `geometry.parameterization-optimization` in a separately reviewed slice.

## Context
- Owner/layer: comparative fixtures, hint interpretation, chart construction, and UV diagnostics live in `src/geometry`; runtime binds the accepted selected-mesh METHOD-038 output contract and owns undoable UV publication; app owns only validated controls and comparison presentation.
- METHOD-038 must either accept the existing `f:curvature_component`, dual-connected `f:curvature_region`, and `e:curvature_region_boundary` publication or add a reviewed continuous surface-curve result with a derived edge visualization. Either form remains non-destructive and is still a hypothesis until this task evaluates atlas behavior.
- `Geometry.UvAtlas` already owns source-face cross-references, chart/seam-cut records, FastStaged/XAtlas selection, per-chart parameterization attempts, quality diagnostics, packing, and corner-domain UV publication. Integration should be an optional input to that surface, not a parallel atlas path.
- Evidence must compare like-for-like atlas runs on the same source mesh and backend, including the current proposal policy, curvature-guided hints, and a deliberately weak control. The task must record negative results rather than silently adopting a visually appealing subset.

## Control surfaces
- Config: add an explicit atlas chart-policy value only if evidence supports integration; `existing` remains the unchanged default until a separate adoption decision is justified.
- UI: reuse the Curvature/Parameterization inspection surfaces to preview segmentation boundaries and run side-by-side atlas diagnostics; applying UV output must use the existing validated runtime history path.
- Agent/CLI: use the same config record, property preflight, atlas request, diagnostics, and publication path as the UI.

## Backends
- Backend axis: evaluate the current CPU FastStaged path first and compare XAtlas only as the existing visible baseline. No GPU path is introduced.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Triangle surface topology plus source positions and slot-aligned connected-region/boundary facts; faces and adjacency are semantic inputs. |
| Compatible entity sources | Mesh entities only. Runtime resolves canonical face/edge sources and rejects missing, stale, mismatched, or ambiguous topology mappings. |
| RuntimeModule | Extend the existing geometry-processing/UV regeneration operation only after the CPU evidence gate; add no service or queue. |
| Config/agent | Any chart-policy selector and thresholds are schema-versioned, serializable, previewed, validated, and applied through the existing config control. |
| UI | Preview accepted/rejected boundaries and side-by-side atlas diagnostics; run and publication use the configured runtime operation. |
| Publication | Preserve source topology. Publish atlas chart/seam records and one authoritative UV domain, preferring `h:texcoord` when cuts require distinct corner values; preserve unrelated properties with undo/redo and coherence revisions. |
| End-to-end tests | Property/topology preflight, deterministic chart constraints, UV quality, fallback, stale rejection, config source parity, UI comparison, publication, and undo/redo. |

## Required changes
- [ ] Freeze a deterministic evaluation corpus covering plane/cylinder/sphere-like patches, saddles, sharp folds, smooth curvature transitions, thin features, open boundaries, disconnected components, and representative imported meshes; record dataset identities and exact source state.
- [ ] Preregister quality gates for finite UVs, zero overlaps/flips where required, conformal/stretch/area diagnostics, seam length, chart count, packing utilization, determinism, perturbation stability, and bounded runtime. Separate hard correctness gates from descriptive trade-offs.
- [ ] Compare the existing FastStaged proposal, curvature-region guidance, XAtlas baseline, and a weak/control partition under identical downstream parameterization and packing settings. Record per-mesh results and aggregate distributions; do not average away fixture regressions.
- [ ] Audit sensitivity over METHOD-038 Fixed/Automatic mode, component range/count, feature policy/scale, dihedral threshold, boundary fairness, spatial weight, and minimum region size. Identify a bounded robust operating region or reject adoption.
- [ ] If and only if the frozen evidence passes, add a plain optional chart-boundary/region-hint input to `Geometry.UvAtlas`; validate exact slot cardinality and connectivity, preserve current behavior when absent, and report requested/accepted/rejected hint edges and fallback reasons.
- [ ] Interpret accepted region boundaries as chart constraints and produce the existing source-referenced chart/seam records. Parameterize each valid chart through existing strategies and pack through the existing atlas path; fail or fall back explicitly for non-disk, degenerate, tiny, or invalid charts.
- [ ] Bind the accepted option through the existing runtime UV-regeneration command, config/agent lane, and Sandbox comparison/apply UI. Revalidate segmentation and source topology revisions before commit.
- [ ] If evidence fails, add a refuted ARA claim and close the task without production adoption; keep METHOD-037 visualization available independently.

## Tests
- [ ] Prove absent/off hints are bit-for-bit equivalent to the current atlas policy and do not alter its default backend or fallback behavior.
- [ ] Prove region IDs and boundary flags produce the same deterministic constrained charts, with exact source-face/corner cross-references and no authoritative mesh-topology split.
- [ ] Cover missing/mismatched/stale properties, disconnected or non-contiguous labels, boundaries, non-manifold/degenerate input, tiny regions, and charts that cannot be parameterized as disks.
- [ ] Assert finite UVs, exact flip/overlap policy, seam metadata, chart count, quality diagnostics, deterministic repeats, and orientation/scale/controlled-noise sensitivity on the frozen corpus.
- [ ] Cover runtime publication, one-authority UV-domain replacement, dirty revisions, undo/redo, no-change behavior, and stale-source rejection.
- [ ] Cover config defaults/round-trip/source parity and UI preview/comparison/explicit-apply behavior.

## Docs
- [ ] Publish the preregistration, raw result JSON, per-fixture comparison report, supported/refuted claim rows, parameter guidance, failure modes, and explicit adoption disposition.
- [ ] Update geometry/parameterization, runtime/config, Sandbox, and UV-domain authority docs only for the behavior actually adopted.
- [ ] Refresh method/benchmark/task inventories and generated module inventory for any new public surface.

## Acceptance criteria
- [ ] A frozen, reproducible comparison demonstrates the preregistered correctness gates and makes the atlas trade-offs inspectable per fixture.
- [ ] Production hint adoption occurs only if every hard gate passes and an independent high-risk fixed-surface review accepts the exact revision; otherwise the current atlas path remains unchanged.
- [ ] Any adopted path preserves source topology, publishes correct corner-domain seams/UVs, and exposes identical validated behavior to config, agent/programmatic callers, and UI.
- [ ] The result makes no broader atlas-quality or performance claim than the checked-in evidence supports.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentation|UvAtlas|Parameterization' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root <sealed-result-dir> --manifests-root benchmarks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
```

## Forbidden changes
- Treating a region color or label discontinuity as a seam without validating the semantic boundary property and source topology revision.
- Mutating the authoritative mesh topology merely to encode UV seams.
- Hiding failed curvature-guided charts behind an unreported baseline fallback.
- Promoting curvature guidance to the default from a single fixture, screenshot, aggregate mean, or unsealed dirty-worktree timing result.
- Mixing atlas adoption with a new solver, packer, clustering implementation, or unrelated optimization refactor.

## Maturity
- Target: `Operational` only if the evidence gate passes and the complete runtime/config/UI/UV-publication path is accepted.
- A negative result is a valid closure: retain the existing atlas policy and record the curvature-guided hypothesis as refuted.
