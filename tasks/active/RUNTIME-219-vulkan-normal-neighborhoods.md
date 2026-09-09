---
id: RUNTIME-219
theme: J
depends_on: [GEOM-077, RUNTIME-213, UI-045]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive implementation; reviewed diff, CPU/Vulkan tests and bounded benchmark records provide evidence."
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-219 — Vulkan LBVH neighborhoods for normal estimation

## Goal
- Add explicit Vulkan LBVH neighborhood execution to the existing normal-estimation workflow on every canonical point-valued property domain, preserving CPU PCA/orientation and publication semantics.

## Scope and design
- User accepted normal estimation first; other consumer migrations remain separate.
- Keep Hoppe PCA/orientation and the existing k+1-then-filter behavior. Mitra–Nguyen–Guibas neighborhood error analysis constrains support preservation. PCPNet and other learned/adaptive estimators are excluded; this changes query execution only.
- Geometry consumes plain supplied neighborhood spans through the existing estimator. Runtime uses the existing cache and JobService dependency chain: GPU queries/readback precede a CPU fit job, so neither the device thread nor a worker blocks waiting on the other.
- Extend the existing framed query implementation with radius batches; preserve total counts and reject incomplete normal neighborhoods. No new module, service, scheduler or universal query interface.
- Radius capacity is bounded by the existing 1024-hit GPU contract; overflow fails without output mutation. Query chunks bound transient buffers. GPU k is 1..64 including the reference's extra candidate. No hidden CPU fallback or default change.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 samples plus complete candidate-neighborhood offsets/indices for the CPU fit. |
| Compatible entity sources | All eight canonical domains, including face/edge/halfedge samples; no conversion. |
| RuntimeModule | Existing geometry-processing owner and SpatialIndexCache own jobs, leases, framing and readback. |
| Config/agent | Add explicit vulkan_lbvh normal backend and validated GPU query batch size to the existing section. |
| UI | Existing normal window selects the same backend/config and states GPU queries with CPU PCA/orientation. |
| Publication | Existing named float3 property transaction, deletion-row preservation, freshness, cancellation and history. |
| End-to-end tests | CPU oracle/supplied-neighborhood tests, unavailable/invalid/stale contracts, actual framed Vulkan normal output versus CPU, radius overflow and cache reuse. |

## Spatial acceleration review
- Euclidean metric in the bound property's space, unchanged entity-transform treatment. Ascending original-slot ties and source-to-compact mapping are preserved; do not replace k+1-then-self-filter with a different duplicate policy.
- Cache leases reuse unchanged positions/deletions and retain submitted buffers. A changed source invalidates publication. Dense radius overflow never counts as complete support.
- Right sizing: plain neighborhood views cross the geometry/runtime boundary; one concrete framed radius operation extends existing allocation/retirement machinery. Two existing JobService jobs are justified by the GPU-to-CPU execution boundary and avoid another scheduler or a blocking worker.

## Acceptance criteria
- [x] CPU supplied-neighborhood estimator matches the unchanged fit/orientation oracle and rejects malformed input.
- [x] Framed Vulkan radius queries preserve total counts, exclusion, reuse and stale-handle behavior.
- [x] Normal config/UI/runtime execute Vulkan kNN and radius neighborhoods with truthful requested/actual reporting.
- [x] Same-domain named publication, deleted-row preservation, cancellation/staleness and undo/redo remain intact.
- [x] Focused/full CPU gates, Sandbox build, actual Vulkan normal/readback comparisons and bounded benchmark diagnostics pass; docs/manifests/inventory synchronized.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke\.' -L gpu -L vulkan --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Review and current evidence
- CPU: Clang 23 preset `ci`; `IntrinsicTests`, `ExtrinsicSandbox` and `IntrinsicBenchmarkSmoke` built. The focused run passed 45/45; the exclusion-only full CPU gate passed 4,376 tests with one expected unsanitized LSan-control skip (4,377 selected). Durable logs and source hashes are in the [verification record](../../ara/evidence/tables/normal_vulkan_verification_2026-09-09.md).
- Structural: strict layering/task/test-layout, method/benchmark manifests, skill mirrors and documentation links passed. Root hygiene reports the existing local `.agents/` metadata finding owned by BUG-177.
- Verification triage: an initial CPU selector accidentally included opt-in Vulkan fixtures; the unavailable-device framed kNN timeout is recorded as BUG-182. The corrected CPU selector excludes capability labels.
- Architecture sweep: no new dependency edge, module, service or shader pass. The existing cache owns buffer lifetime; CPU geometry sees spans only. Both dependent jobs identify the same output; cancellation/staleness prevents publication and submitted buffers remain leased through readback.
- Clean-workshop scorecard: layer imports, CMake links and exported ownership pass; renderer growth, new pass IDs, recipe edges, scaffold closure and temporary exceptions are not applicable.
- Scientific limits: no GPU eigensolver/orientation claim, no speedup claim or backend-default change; radius truncation is a failure, not an approximation. C80 binds the bounded actual Vulkan result.

- Vulkan: RTX 3050 driver 590.48.01, Clang 23 `ci-vulkan` with ASan+UBSan. Four existing cases passed; two new cases passed after correcting premature cold-start readiness skips. Normal outputs match CPU on eight domains (maximum component error zero, tolerance 1e-5). Radius overflow, source exclusion/reuse, stale/cancelled output retention and undo/redo pass. Existing leak-detection exclusions remain owned by BUG-180.
- Benchmark: `geometry.point_lbvh.normal_runtime_smoke` produced and validated schema-v2 output, passing the unchanged 15,000 ms/1e-5 smoke gates. Warm wall time 9,822.29 ms versus a separate 168.04 ms CPU reference on this tiny fixture; no speedup or default change. GPU timers include frame waits/readback, and per-request elapsed sums overlap.
- Completion: implemented and locally verified at **Operational** for the bounded query/CPU-fit workflow. No commit/push yet; keep this note active until publication supplies its retirement reference.
