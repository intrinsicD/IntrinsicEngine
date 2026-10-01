---
id: GEOM-115
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: mechanical rename with no semantic change (AGENTS.md §5); evidence is the build, the existing test suite, the inventory regeneration and the layering checks.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GEOM-115 — Make 13 halfedge-mesh module names match their file stems

## Goal
Remove the violation of the documented geometry naming rule for thirteen modules, in one
mechanical change with no semantic edits.

## Context
Source: 2026-10-01 duplication/consistency audit (finding 2.1), re-verified at `665c693dd`.
`docs/architecture/geometry-api-style.md` ("Module, file, and namespace style") says to name module
interface files `Geometry.<Concept>[.<Subconcept>].cppm` and "use an exported module name that matches
the file stem". Thirteen `src/geometry/Geometry.HalfedgeMesh.<X>.cppm` files export `Geometry.<X>`
instead (other `Geometry.HalfedgeMesh.*` files, for example `Analysis`, `Boundary`, `Builder`, `IO`,
`Quality`, `Repair`, `Features`, `Utils` and `SubdivisionSqrt3`, already match):

Boolean, CatmullClark, Curvature, DEC, Geodesic, ModalAnalysis, Parameterization, Remeshing,
SignedHeatMethod, Simplification, Smoothing, Subdivision, VectorHeatMethod.

Tooling impact checked while writing this note:
- `tools/repo/generate_module_inventory.py` reads exported module names from `*.cppm`;
  `docs/api/generated/module_inventory.md` must be regenerated either way.
- `tools/repo/check_layering.py` and `layering_allowlist.yaml` work on module identifiers; a rename of
  the module names changes identifiers in any allowlist entry that names them (grep before editing).
- A module-name rename (Option A below) changes the `import Geometry.<X>;` lines in about 66 files
  across `src`, `tests`, `tools`, `benchmarks` and `methods`, and any documentation naming the modules.
- A file rename (Option B) leaves importers untouched but moves file paths that are cited elsewhere:
  `benchmarks/ci/manifests/geom099_reuse_compile.yaml` (Parameterization sources), ARA claim citations
  (BUG-224 fixed an earlier renamed-path breakage), documentation links, and links inside retired
  `tasks/done/*` notes, which must not be edited and would fail `check_doc_links.py`.
- Name collisions to check: `Geometry.Parameterization.{Bff,Diagnostics,Harmonic,Optimize,Types}`,
  `Geometry.Smoothing.Types` and `Geometry.Geodesic.Types` already exist as distinct files; confirm that
  renaming a module or file stem neither collides with nor misleads beside them.
- Related, left out on purpose: 323 modules start with `Extrinsic.` and 132 with bare `Geometry.`,
  which appears intentional but is undocumented in `docs/architecture/module-rules.md`; ECS component
  modules mix `Component`/`Components`; a few other modules (`Platform.IWindow`,
  `ECS.Component.Transform.*`, `Sandbox.EditorShell`/`EditorController`) do not match their stems.
  Record those as a follow-up inventory note rather than widening this task.

## Decision needed (operator): which side moves
- **Option A — rename the module names** to `Geometry.HalfedgeMesh.<X>` to match the files. No file or
  path moves (history, ARA citations, manifests and done-task links stay valid); about 66 import
  sites change. The module names become longer and differ in prefix from siblings such as
  `Geometry.Smoothing.Types`.
- **Option B — rename the files** (and their `.cpp`) to `Geometry.<X>.cppm` to match the modules.
  Importers are unchanged and the short names survive, but it moves 26 files and breaks the path
  citations listed above.
- **Option C — keep both and amend the rule**, documenting the thirteen as sanctioned exceptions.
  Zero churn; the rule stops being checkable.

This note leans to Option A for its smaller blast radius, but the choice changes a documented
convention and belongs to the operator.

## Acceptance criteria
- [ ] The chosen option is recorded in the task log; the diff contains only renames and the import/reference updates they force.
- [ ] No exported module name differs from its file stem in `src/geometry`, or the rule in `geometry-api-style.md` states the sanctioned exceptions.
- [ ] `docs/api/generated/module_inventory.md` is regenerated; `check_layering.py`, `check_docs_sync.py` and `check_doc_links.py` pass; no allowlist or manifest cites a stale name or path.
- [ ] Full default build and CPU test gate pass with identical results (no test expectation changes).

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
python3 tools/repo/generate_module_inventory.py --root src --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
