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
the file stem". At the audit baseline, thirteen `src/geometry/Geometry.HalfedgeMesh.<X>.cppm` files exported
`Geometry.<X>` instead (other `Geometry.HalfedgeMesh.*` files, for example `Analysis`, `Boundary`, `Builder`, `IO`,
`Quality`, `Repair`, `Features`, `Utils` and `SubdivisionSqrt3`, already match):

Boolean, CatmullClark, Curvature, DEC, Geodesic, ModalAnalysis, Parameterization, Remeshing,
SignedHeatMethod, Simplification, Smoothing, Subdivision, VectorHeatMethod.

Original planning constraints (before implementation):
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

## Operator decision and implementation
On 2026-10-16, the operator selected **Option A — align module names with file names**
(“Modulnamen an Dateien angleichen”). The thirteen exported names and their exact in-tree
imports/references were changed to `Geometry.HalfedgeMesh.<X>` under
[HARDEN-090](../backlog/workshop/HARDEN-090-src-audit-remediation.md). Source file paths remain unchanged;
this patch changes no behavior or test expectations. The inventory was refreshed with the
combined mechanical integration.

The alternatives were file renames (Option B) or documented naming exceptions (Option C);
neither was selected. The default build, CPU integration and native follow-ups ran under
HARDEN-090; its canonical record distinguishes passes, corrections and host skips.
Formal acceptance and retirement remain separate below.

## Acceptance criteria
- [x] The chosen option is recorded in the task log; the diff contains only renames and the import/reference updates they force.
- [x] No exported module name differs from its file stem in `src/geometry`, or the rule in `geometry-api-style.md` states the sanctioned exceptions.
- [x] `docs/api/generated/module_inventory.md` is regenerated; `check_layering.py`, `check_docs_sync.py` and `check_doc_links.py` pass; no allowlist or manifest cites a stale name or path.
- [x] Full default build and CPU test gate pass with identical results (no test expectation changes).

Acceptance source: independent `/root/implement_platform_assets` reconciled all
four criteria against the mechanical patch and existing build/inventory/check
receipts. The CPU criterion uses the full block-19 run plus focused correction
and host-permission rechecks under HARDEN-090. The original full run exited 8;
no second unfiltered green run is claimed or required by this reconciliation.
Formal retirement/commit association remains with HARDEN-090.

## Verification
```bash
cmake --build --preset ci --target all IntrinsicTests
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
python3 tools/repo/generate_module_inventory.py --root src --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Completion
Completed 2026-10-19. Commit: `149172027`. Acceptance as recorded above.
In addition, main [`ci-linux-clang` run 39168698007](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/39168698007)
on `64d691ae5` builds the default CPU cohort and runs the full CPU suite, which reports 0 failures of 4076 (7 skipped at runtime);
this is the unfiltered green run the reconciliation above did not have.
