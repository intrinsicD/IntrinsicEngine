---
id: GEOM-116
theme: I
depends_on: []
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the 2026-10-01 audit; implementation owes its own tests, gpu;vulkan smoke and parity evidence for any claim.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
---
# GEOM-116 — Vulkan Tournament point sampling

## Goal
Add a selectable Vulkan backend for the existing Tournament sampler so it is no longer the only
data-parallel point sampler without a GPU path. The CPU reference stays canonical truth; the GPU backend
reports backend identity and a parity delta.

## Context
Source: 2026-10-01 duplication/consistency audit (finding 5.1), re-verified at `665c693dd`. This is a
backlog note, not a speedup claim: nothing was measured.

- CPU reference: `TournamentOrder` in `src/geometry/Geometry.PointSampling.Approximate.cpp`. It builds a
  Morton-ordered balanced tree (the original CUDA method uses a Karras LBVH): leaves are single points;
  an internal node keeps the child winner nearer its box center (ties: smaller index) and records the
  other as its loser. The order is the root winner, then the losers by node box diagonal, largest first
  (ties: seeded random key of the node, then index).
- Existing GPU pieces that map onto the stages: `lbvh_bounds.comp`, `lbvh_morton.comp`,
  `RecordGpuRadixSort` (`Graphics.ComputeParallelPrimitives.cppm`), `lbvh_build.comp`, and the Vulkan
  farthest-point sampler (`Graphics.FarthestPointSampling.cpp`, `point_sampling_farthest.comp`) as the
  integration precedent. Inputs are already on the residency.
- New work: a bottom-up tournament pass over the tree (a refit-style walk, level by level, with the
  deterministic tie rule) and a second radix sort of the losers by node diagonal with the seeded key.
  Decide whether the GPU path uses the same balanced tree as the CPU reference (preferred for parity)
  or the Karras LBVH, and record the reason.
- Allocate workspaces device-local, not `HostVisible` (see the GEOM-103 note on the 2026-10-01 finding
  about host-visible iterative workspaces).
- Owners: the point-sampling runtime module (`src/runtime/Modules/PointSampling`) and
  [GEOM-111](../../active/GEOM-111-unified-progressive-point-sampling.md); coordinate with GEOM-111 so
  the progressive/unified sampler contract is not forked.

Noted, not in scope ("could", from the same audit): weighted sample elimination
(`SampleEliminationOrder`, same file) could take a GPU weight-initialization stage (a radius query plus a
weight kernel) while the heap elimination stays sequential. Evaluate it only after this task, and only if
the initialization is shown to dominate; it needs its own measurement.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Canonical positions of any compatible point-property domain. |
| Compatible entity sources | All compatible point-property domains, as for the existing point-sampling operation. |
| RuntimeModule | Existing point-sampling module; Tournament gains a Vulkan backend option, no new module. |
| Config/agent | Backend selection through the existing point-sampling config section and agent operation, with requested/actual/fallback reporting; no new control surface beyond that option, and file/agent/UI parity. |
| UI | The existing Point Sampling panel shows the backend and fallback reason through its spec widgets. |
| Publication | Unchanged: the same sampled order/mask as the CPU path, through the existing publication. |
| End-to-end tests | CPU-oracle comparison, actual GPU readback, stale/cancelled work and config/UI coverage for the new backend value. |

## Acceptance criteria
- [ ] The GPU order matches the CPU `TournamentOrder` on the shared fixtures, or a parity delta is declared, justified and reported; ties, duplicate points, `count` of 1 and `count == n` are covered.
- [ ] Deterministic across runs and workgroup scheduling (no floating-point atomics; seeded keys only).
- [ ] CPU/oracle tests in the default gate; `gpu;vulkan` smoke proves actual GPU execution (a fallback or a skipped test is not proof).
- [ ] Backend identity and fallback reasons are reported through the existing path; the CPU reference and default backend are unchanged.
- [ ] Benchmark through the benchmark workflow before any speed statement; docs and method manifest updated.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointSampling' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'PointSampling' --no-tests=error --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
