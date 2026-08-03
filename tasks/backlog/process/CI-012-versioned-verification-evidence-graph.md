---
id: CI-012
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# CI-012 — Compile a versioned verification evidence graph

## Goal

- Produce one schema-versioned, deterministic evidence graph that reconciles
  the current configured build targets, test producers/cases, variants,
  capabilities, and contract proofs without changing which checks are
  authoritative.

## Non-goals

- No affected-test selection, result caching, build-backend replacement, or CI
  workflow cutover.
- No second test-authoring format and no broad rewrite of existing test CMake.
- No performance claim from local or unmatched graph-generation timings.

## Context

- Owner: `tools/ci`, CMake test registration, and verification metadata; no
  engine layer is changed.
- This task is the compatibility foundation in
  [`verification-evidence-architecture.md`](../../../docs/architecture/verification-evidence-architecture.md).
- The graph is a plain generated artifact compiled from current authorities.
  Non-derivable isolation/resource/cache policy belongs at the owning test
  registration site; derived target, case, and contract relationships must not
  be copied into a hand-maintained manifest.
- This task introduces a reusable verification contract. Before implementation
  begins, add its canonical prose, stable catalog ID, and executable proof in
  the same change, then update this task's `contracts` declaration.

## Right-sizing

- Element: the compiled evidence graph could become a registry/database
  framework.
- Simpler alternative: generate one versioned JSON record set with plain
  validation/query functions from existing authorities.
- Blast radius: `cmake/`, test registration, `tools/ci`, contracts, tests, and
  docs only; no `src/` dependencies or runtime owner.
- Reintroduction trigger: a persistent store or service is reconsidered only
  after measured graph size/concurrency makes deterministic file generation a
  material bottleneck and a separate task names the present consumers.

## Control surfaces

- Config: schema-versioned checked-in verification policy for non-derivable
  metadata only.
- UI: N/A; this is repository tooling.
- Agent/CLI: read-only graph compilation, validation, and explanation commands.

## Required changes

- [ ] Define a versioned graph schema for source/build inputs, targets/actions,
      logical tests, variants, capabilities/resources, contracts/proofs, and
      result provenance.
- [ ] Compile configured CMake codemodel/test inventories, CTest JSON,
      GoogleTest case identities, preset identity, and the contract catalog
      into one deterministic graph under the build evidence directory.
- [ ] Preserve the current unique source → object library → executable owner
      and aggregate membership checks as graph invariants.
- [ ] Record provenance and content digests for every input; reject duplicate
      identities, missing producers/proofs, unknown schema versions, and
      incomplete configured registries.
- [ ] Add a legacy-parity projection that compares graph-derived producers,
      cases, labels/capabilities, and aggregates with the current registry
      outputs without changing gate selection.
- [ ] Add canonical contract prose, a stable catalog entry, and executable
      proof for the graph once its final schema is fixed.
- [ ] Emit a bounded human-readable summary and stable machine JSON; generated
      artifacts must not become checked-in mutable state.

## Tests

- [ ] Add regression fixtures for deterministic ordering/digests, duplicate
      identities, missing producers, stale catalog proofs, unknown schema
      versions, and configure-identity changes.
- [ ] Reconcile the exact live logical case set and every current required
      variant/capability projection with the legacy registries.
- [ ] Prove a clean configure and an unchanged reconfigure emit byte-identical
      normalized graphs.
- [ ] Prove malformed or incomplete inputs fail closed instead of emitting an
      empty or partial success graph.

## Docs

- [ ] Update `docs/architecture/test-strategy.md`, `tests/README.md`, and
      `tools/ci/README.md` with graph ownership, provenance, and compatibility
      rules.
- [ ] Add the implemented stable contract to
      `docs/architecture/contract-catalog.yaml` with canonical source and
      executable proofs.
- [ ] Update the roadmap task status without describing later routing or cache
      tasks as implemented.

## Acceptance criteria

- [ ] One command emits a validated graph for each supported preset identity.
- [ ] The graph and legacy authorities have exact producer, logical-case,
      variant, capability, and contract-proof parity on the full configured
      registry.
- [ ] Identical inputs emit identical normalized bytes; any material input
      change alters the relevant identity.
- [ ] No workflow, selector, or default test command changes authority in this
      task.
- [ ] The reusable graph contract has canonical prose, a catalog entry, and a
      regression proof in the same reviewed change.

## Verification

```bash
cmake --preset ci-fast --fresh
cmake --build --preset ci-fast --target IntrinsicPrFastTests
python3 tools/ci/verification_manifest.py --root . --build-dir build/ci-fast --check --output build/ci-fast/verification/graph.json
python3 tests/regression/tooling/Test.VerificationManifest.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Making labels, a new YAML list, or task prose a second independent routing
  authority.
- Hand-maintaining relationships that CMake, CTest, compiler metadata, or the
  contract catalog can derive.
- Weakening existing registry, aggregate, capability, or configure-determinism
  checks to obtain parity.
- Modifying production engine code.
