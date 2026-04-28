# ADR O1 — Minimal Runtime Refactor Path

- **Status:** Accepted as a documented fallback option
- **Date:** 2026-03-19
- **Owners:** Runtime / Rendering Architecture
- **Related backlog:** `TODO.md` → `B3. Engine Architecture Review Follow-Up`
- **Related specs:** `docs/architecture/runtime-subsystem-boundaries.md`, `docs/architecture/rendering-three-pass.md`

## Context

The runtime already completed the first major rendering refactor: ownership is split across `GraphicsBackend`, `AssetPipeline`, `SceneManager`, `AssetIngestService`, and `RenderOrchestrator`, while the renderer itself follows the canonical three-pass architecture plus deferred/post-process overlays.

The remaining architectural question is not whether to rewrite again immediately, but how aggressively to evolve `Engine::Run()` and the surrounding runtime orchestration.

Option **O1** is the conservative path: keep the current ownership graph and lane split intact, fix the highest-friction seams, add tests/telemetry around the existing flow, and avoid introducing new frame abstractions until the current topology demonstrably blocks performance, correctness, or testability.

Formally, if the current runtime composition is modeled as a directed ownership graph

$$
G = (V, E), \qquad V = \{\text{Engine}, \text{GraphicsBackend}, \text{AssetPipeline}, \text{SceneManager}, \text{AssetIngestService}, \text{RenderOrchestrator}\},
$$

then O1 keeps $V$ and the dominant ownership edges $E$ stable, and only reduces accidental coupling edges $E_a \subseteq E$ that increase maintenance cost without changing behavior.

The optimization goal is therefore not a new architecture, but a bounded reduction of orchestration entropy:

$$
\min \; C_{maint} + C_{risk}
$$

subject to preserving current frame behavior, render-pass ordering, and resource-lifetime invariants.

## Decision

Adopt **O1** as a documented fallback architecture option for the B3 review package.

Under O1 we will:

1. keep the existing subsystem split and borrowed-reference topology,
2. keep `Engine::Run()` as the top-level composition root,
3. prefer small seam extraction over new runtime-wide abstractions,
4. add tests and telemetry before any deeper migration,
5. delete temporary adapters immediately if they stop carrying real value.

This ADR does **not** replace the pending O2/O3 decision work. It captures the smallest coherent path so future review can compare O1 against more ambitious options on explicit criteria rather than intuition.

## Benefits

### 1. Lowest regression surface

O1 preserves the current execution order, resource ownership, and ECS/render synchronization model. That minimizes the probability of regressions in GPUScene lifecycle handling, asset-upload retirement, render-graph construction, and editor interaction flow.

### 2. Fastest path to measurable hardening

Because the ownership graph remains stable, effort can be redirected toward contract tests, telemetry baselines, and localized cleanup. This increases confidence without paying an up-front rewrite tax.

### 3. High code-review clarity

Small seam-focused changes are easier to review, bisect, and revert. For a codebase with heavy C++23 modules and Vulkan lifetime rules, this matters more than abstract architectural neatness.

### 4. Preserves current performance characteristics

O1 avoids adding extra frame copies, snapshot layers, or packet transforms before there is data proving they are needed. Complexity remains approximately linear in the number of currently orchestrated systems and render passes:

- runtime orchestration overhead: $O(S + P)$ for $S$ registered systems and $P$ render passes,
- auxiliary memory growth: $O(1)$ beyond existing per-frame allocator usage,
- migration overhead: bounded to the touched seams rather than the whole runtime.

### 5. Better fit for current active backlog

Several near-term TODO items are still validation- and correctness-oriented rather than architecture-blocked. O1 lets the team finish those without coupling them to a larger runtime redesign.

## Drawbacks

### 1. Leaves `Engine::Run()` as the central orchestrator

O1 reduces friction but does not fundamentally change the top-level loop shape. The engine remains explicit and understandable, but the main loop continues to own a large share of sequencing responsibility.

### 2. Testability improves incrementally, not structurally

Without immutable extraction types or explicit frame contexts, test seams remain narrower than in O2/O3. We can test more of the current flow, but not as cleanly isolate every stage.

### 3. Limited future-proofing for multi-lane evolution

O1 does not proactively establish the fixed-step/extraction/submission/maintenance pipeline envisioned in B4. Future migration work may therefore revisit some seams twice.

### 4. Coupling hotspots remain visible

Borrowed cross-links between runtime subsystems stay explicit. That is acceptable in the short term, but it means O1 controls coupling rather than redesigning it away.

## Migration Cost

**Low.**

Expected work under O1 is documentation, contract clarification, seam extraction, and targeted tests. No broad module re-layout is required. Typical changes are:

- moving a small orchestration concern into an existing helper,
- adding a typed helper bundle around current flow,
- codifying lifecycle invariants in tests,
- adding telemetry snapshots for frame order or resource retirement.

This is intentionally a low-churn path compatible with incremental PRs.

## Regression Risk

**Low, but not zero.**

O1 is safer than O2/O3 because it preserves stable ownership and pass ordering. The main remaining risks are local refactor slips:

- shifting completion/event order by one frame,
- changing cleanup timing,
- drifting markdown/contracts away from code.

These risks are best controlled with targeted integration tests and telemetry gates rather than broader abstraction.

## Performance Impact

**Near-zero by default; slightly positive if cleanup removes redundant work.**

O1 should be performance-neutral unless a specific hotspot is improved. It is unlikely to unlock major new wins by itself, but it also avoids introducing new CPU overhead.

In optimization terms, O1 assumes the current frame cost is dominated by existing system/render work rather than orchestration overhead:

$$
T_{frame} \approx T_{systems} + T_{render} + T_{sync},
$$

with any O1 gains coming from reducing unnecessary constant factors inside $T_{sync}$ and glue code.

## Testability Impact

**Moderate improvement.**

O1 supports the following practical test gains without a deeper redesign:

- freeze current frame/system/pass ordering with regression tests,
- add seam-level tests for frame-loop helpers,
- codify resource-retirement and dispatcher-order behavior,
- strengthen render-graph contract suites around the existing pipeline.

It does **not** by itself deliver the stronger isolation that immutable `RenderWorld` extraction or `FrameContext` rings would provide.

## Future Extensibility Impact

**Adequate for near-term work, weaker for long-horizon redesign.**

O1 leaves room for the current roadmap items, but it does not create first-class architecture for them. Features such as bounded frames in flight, queue-domain-aware scheduling, immutable extraction, or GPU-driven packet preparation will still require a later structural migration.

Therefore O1 is extensible in the sense of *not blocking* future work, but not in the sense of *prepaying* for that future work.

## Consequences

### What becomes easier immediately

- documenting and enforcing current subsystem boundaries,
- keeping the runtime stable while P1 validation work lands,
- reducing localized code duplication and sequencing ambiguity,
- shipping low-risk cleanup PRs with measurable regression coverage.

### What remains deferred

- explicit `FrameContext` ownership,
- immutable render extraction types,
- full fixed-step/extraction/render-prep/submission/maintenance staging,
- queue-domain-aware renderer execution architecture.

## Guardrails

If O1 is used as the active path for any period, the following guardrails apply:

1. No new hidden coupling through globals, file-static state, or service-locator style access.
2. Any new helper must preserve existing main-thread ownership and lifecycle invariants.
3. Tests must be added before and after touching frame ordering or cleanup code.
4. Temporary compatibility shims must be deleted once their migration window closes.
5. Markdown architecture docs must remain synchronized in the same change series.

## Review Trigger

Re-open O1 and escalate toward O2 if any of the following become true:

- frame-loop ordering changes routinely require risky cross-subsystem edits,
- render preparation needs immutable snapshots to remain correct,
- bounded frames-in-flight or queue-domain scheduling becomes an immediate product requirement,
- telemetry shows orchestration overhead is materially hurting the frame budget,
- seam-level tests can no longer cover the runtime behavior being changed.

## Rationale Summary

O1 is the correct minimal baseline because it optimizes for stability, observability, and low migration cost while preserving the current runtime and render contracts. It is intentionally conservative: valuable when the architecture is serviceable and the next wins come from hardening rather than reinvention.
