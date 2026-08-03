---
id: CI-014
theme: H
depends_on: [CI-012, CI-013]
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
# CI-014 — Derive the static build and contract impact graph

## Goal

- Derive a fail-closed source/build-input → target/module → reverse-dependent
  → contract-proof closure that can explain the exact build and proof actions
  affected by a change without hand-maintained path routing.

## Non-goals

- No coverage-derived test selection; `CI-018` owns the admitted hybrid union.
- No claim that static dependencies alone identify every behaviorally relevant
  test.
- No new production module or dependency edge.

## Context

- Owner: CMake/compiler metadata and `tools/ci` impact planning.
- The graph must cover C++23 named modules as well as header/include, generated
  source, shader, asset, CMake, toolchain, dependency-lock, and contract-proof
  inputs. Module imports alone are insufficient.
- `CI-013` remains the only planning entrypoint; this task adds a provider to
  its evidence graph rather than a second selector.
- Unknown or incomplete dependency data must broaden to a bounded aggregate or
  fail, never return no work.

## Right-sizing

- Element: impact discovery could become a parallel dependency database and
  selector framework.
- Simpler alternative: compile CMake/compiler/catalog facts into the existing
  graph and answer reverse-closure queries with plain functions.
- Blast radius: configured build metadata, contract proofs, `tools/ci`, and
  regression fixtures only; layering is checked and production modules do not
  depend on the planner.
- Reintroduction trigger: a separate graph engine is considered only if the
  generated graph fails a declared scale budget and one implementation cannot
  serve all present verifier profiles.

## Required changes

- [ ] Derive source/compile/link/generated-input ownership and reverse target
      closure from the CMake File API, compilation database, Ninja/CMake
      dependency data, and compiler named-module dependency output.
- [ ] Normalize paths and generated inputs so equivalent clean build roots
      produce the same logical graph identity.
- [ ] Map changed contract canonical sources and proof paths to stable catalog
      IDs and schedule every proof for an affected contract.
- [ ] Define explicit fail-closed handling for headers, module interfaces,
      CMake/preset/toolchain/vcpkg inputs, renames/deletes/type changes,
      shaders/assets, missing refs, and unknown paths.
- [ ] Replace duplicated path-table interpretation inside the compatibility
      planner with graph queries while retaining shadow comparison with the
      current `touched_scope.py` routes.
- [ ] Emit bounded `explain` paths from each changed input to every selected
      build target and contract proof, including fallback reasons.

## Tests

- [ ] Add synthetic and live fixtures for `.cpp`, `.cppm`, headers, generated
      inputs, shaders/assets, CMake/presets, dependency locks, task/contract
      sources, rename/delete ambiguity, and unknown paths.
- [ ] Prove reverse closure includes module-interface consumers and ordinary
      include/link consumers without crossing nonexistent edges.
- [ ] Replay a bounded historical diff corpus and compare graph explanations
      with current planner output; any narrower result requires a separately
      justified proof and remains non-authoritative until `CI-018`.
- [ ] Mutate/remove dependency inputs and prove the planner broadens or fails
      rather than silently missing a target/proof.

## Docs

- [ ] Document data sources, normalization, blind spots, fallback classes, and
      `explain` output in `tools/ci/README.md` and test strategy.
- [ ] Update the roadmap task state without claiming dynamic/behavioral impact
      selection.

## Acceptance criteria

- [ ] Every supported changed-input class has a deterministic explained path
      to its owning and reverse-dependent build actions and contract proofs.
- [ ] No manual owner/path map remains authoritative for classes derivable from
      build or contract metadata.
- [ ] Missing or ambiguous evidence broadens or fails closed; it cannot emit an
      empty successful C++ plan.
- [ ] Existing planner behavior remains the control until hybrid selection is
      admitted by `CI-018`.

## Verification

```bash
cmake --preset ci-fast --fresh
python3 tools/ci/verify.py plan --profile pr --base-ref origin/main --head-ref HEAD --explain --output build/ci-fast/verification/impact-plan.json
python3 tests/regression/tooling/Test.VerificationImpactGraph.py
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Encoding C++ target/module dependency relationships as a replacement manual
  path dictionary.
- Using only module imports while ignoring headers/includes, link edges, or
  generated inputs.
- Narrowing unknown, deleted, renamed, or graph-affecting changes to a focused
  route.
- Modifying production engine code.
