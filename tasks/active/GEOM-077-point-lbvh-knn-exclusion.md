---
id: GEOM-077
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: codex-interactive
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-08T15:00:00Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# GEOM-077 — Shared point-LBVH k-nearest and exclusion queries

## Goal
- Extend shared spatial queries with deterministic k-nearest and original-slot exclusion, and supply the existing PCA normal estimator with a reusable CPU LBVH.

## Non-goals
- No algorithm/default change for PCA, ICP or other consumers; no new service, topology mutation, predicate/subset query, ray or triangle acceleration.
- Normal config/UI/property-publication expansion remains RUNTIME-213/UI-045; outlier adoption remains RUNTIME-209/UI-041.

## Context
- The operator requested continuing the recommended steps after ICP. Exact k-nearest/self exclusion is the next prerequisite for Framework24 W5 point analyses and normals, with multiple concrete consumers in the spatial inventory.
- Preserve the Karras 2012 radix hierarchy and exact float leaf predicates. CPU exhaustive sorting is the oracle; bounded heap traversal and Vulkan sorted insertion implement the same order. Hoppe PCA/MST and Mitra–Nguyen neighborhood analysis inform the unchanged normal formulation; robust/learned and adaptive-neighborhood estimators are excluded.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 span; optional source ID excluded per query. |
| Compatible entity sources | All canonical domains through the existing cache; no conversion. |
| RuntimeModule | Extend SpatialIndexCache CPU queries and framed GPU batches; immutable snapshots retain current ownership. |
| Config/agent | Query arguments are explicit public API data. Existing method defaults unchanged; normal configuration adoption owned by RUNTIME-213. |
| UI | Existing methods remain unchanged; RUNTIME-213/UI-045 own normal property-domain/config/UI integration. |
| Publication | Query results only; supplied-index PCA returns same-cardinality normals, with no entity mutation. |
| End-to-end tests | All-domain cache mapping, lease/revision behavior, framed readback with exclusions and reuse, PCA backend parity. |

## Spatial acceleration review
- Squared Euclidean metric in the declared index space; ascending distance then original slot tie order. Exclusion is by identity, so coincident other samples remain eligible.
- CPU k=0 returns empty and k>N returns all eligible rows. GPU k=1..64 is explicit and rejects unsupported sizes; complete radius count/overflow behavior is retained. No silent support truncation.
- Stable entity inputs reuse the existing cache; temporary supplied normal indices remain caller-owned. The PCA adapter retains its existing k+1-then-filter policy, including duplicate-position behavior, to avoid a numerical policy change.
- Right sizing: extend concrete existing owners and overloads; no universal neighbor service/interface. A supplied LBVH is a present second index implementation, and runtime framing is an existing layering boundary.

## Required changes
- [x] Add independent CPU oracle, bounded CPU traversal, exclusions and deterministic ordering.
- [x] Extend Vulkan queries and framed cache batches with exact output counts and exclusions.
- [x] Add a checked supplied-LBVH PCA overload without changing its estimator or default.

## Tests
- [x] Analytic, random, tied/coincident, deleted-slot, empty/singleton, k bounds, invalid-input and cache-reuse tests.
- [x] Compare Vulkan against exhaustive CPU and PCA against its existing KD-tree backend.
- [x] Build configured presets and run focused, full CPU and actual Vulkan checks.

## Docs
- [x] Update method/API/consumer docs and relevant task reminders, including remaining adoption owners.
- [x] Record cold/warm CPU query and complete PCA costs with a stable manifest and bounded evidence.

## Acceptance criteria
- [x] Queries preserve primitive, metric, source identity and requested support semantics.
- [x] Engine consumers can acquire/reuse k-nearest queries without a private service or index rebuild.
- [x] All declared tests pass; no default or performance claim without evidence.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicCpuTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke\.' -L gpu -L vulkan --timeout 120
bash tools/ci/run_clean_workshop_review.sh . --strict
```

## Forbidden changes
- No radius truncation presented as kNN, distance-based self removal, CPU work disguised as Vulkan, topology/provenance eligibility restriction, or hidden fallback.

## Review
- Architecture: geometry owns CPU queries/PCA; graphics owns device workspaces; runtime extends its existing frame participant, snapshots and readback. No new layer boundary, facade or scheduler was introduced. Query arguments are API data; normal method configuration/publication stays with RUNTIME-213/UI-045.
- Shader ABI: QueryPush is 72 bytes in both C++ and scalar-layout GLSL, including k at offset 60 and the exclusion address at offset 64. BuildPush remains 64 bytes. The three-argument nearest shader helper remains available to k-means.
- Lifetime: framed batches retain targets and four buffers through submission/readback. Reuse checks query count and k; stale-before-record fails, submitted snapshots remain leased, and method publication retains its own freshness check.
- Clean-workshop scorecard: rows 1–4 pass (strict imports/links, public surfaces and existing workspace owner); rows 5–6 n/a (no new frame passes/recipe edges); row 7 pass (explicit deferred adoption owners); row 8 n/a (no new exceptions).
- Result scope: exact-query fixture parity and supplied CPU PCA fixture parity only. No default change, GPU PCA solve, general speedup or full normal UI completion is claimed.
- Structural checks pass for layering, CMake links, task policy, documentation links, test layout, method/benchmark manifests and skill mirrors. Root hygiene still reports the pre-existing local `.agents/` entry tracked by BUG-177; this slice does not change root policy.
- Initial Vulkan run: standalone nearest/radius, new kNN/exclusion oracle and framed ICP passed. The new framed-kNN fixture exited during expected cold-start validation; corrected the fixture to wait for operational promotion within its existing deadline. This was test lifecycle handling, not a query-oracle tolerance change.

## Verification result (2026-09-08)
- Implementation complete in the shared working tree; review/commit and formal retirement remain pending.
- Clang 23 `ci`: configured, built `IntrinsicGeometryTests`, `IntrinsicCpuTests` and `IntrinsicBenchmarkSmoke`; five focused PointLBVH tests passed before GPU work. Final full CPU selector: 4,352 selected, 4,351 passed, one expected unsanitized `GlfwLifecycleLsan` skip, zero failures (107.53 s).
- Clang 23 `ci-vulkan` with ASan+UBSan: built PointLBVH and clustering smoke targets. Standalone nearest/radius, new kNN/exclusion and existing framed ICP passed; after correcting only the cold-start fixture wait, framed kNN and existing Vulkan k-means passed. Five current capability cases passed, none skipped. This cohort uses its existing leak-disabled test environment; it does not close BUG-180 or prove whole-process leak freedom.
- CPU smoke runner emitted 34 passing results; all seal and validate under schema v2 as dirty-source, non-claim-eligible artifacts. New fixture has zero neighbor mismatches and zero normal component error. Supplied-LBVH normal time exceeds KD-tree time on this small fixture; preserve the default. Separate build/query/PCA timings are diagnostic, with background compilation present during this local run.
- All automated clean-workshop rows, task policy, method/benchmark manifests, skill mirrors, documentation links, test layout and diff whitespace checks pass. RUNTIME-213/UI-045 own normal config/UI/publication; RUNTIME-209/UI-041 own outlier adoption.
