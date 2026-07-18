# ADR 0026: Runtime-module scope by composition and consumer cohesion

- **Status:** Accepted
- **Date:** 2026-07-18
- **Owners:** Runtime / Architecture
- **Related tasks:** `ARCH-015`, `REVIEW-003`
- **Related ADRs:** [ADR-0024](0024-kernel-module-architecture.md) D1/D2/D3/D9/D12

## Context

ADR-0024 separates the slim runtime kernel from app-composed domain
responsibilities, but it does not decide whether two runtime-integrated methods
belong to one composed module or separate modules. A family-first answer such
as one module each for clustering, registration, reconstruction, or PDE
methods would create boundaries before production composition and consumers
exist. Result shape is also insufficient: two methods can both emit integer
labels while differing in lifetime, cancellation, commit, diagnostics, and
the reactions that consume those labels.

The current repository supplies one production case:
`Extrinsic.Runtime.ClusteringModule`, which integrates K-Means. Its command,
status, completion, and output storage are explicitly K-Means-specific; its
sole generic-named labels-changed reaction currently has only a K-Means
producer. Test doubles are not a second production integration. There is no
production DBSCAN or hierarchical-clustering runtime integration, and the
planned GMM work is lower-layer geometry numerics for CPD and CLOP consumers
rather than a second runtime clustering integration. K-Means'
`UseHierarchicalInitialization` option is a seeding policy, not a separate
hierarchical-clustering integration.

## Decision

This decision applies **only after** a responsibility already qualifies for
runtime composition under ADR-0024. An algorithm implementation that needs no
runtime-owned lifecycle, durable state, services, jobs, commit, or standing
consumer reaction remains in its owning method or lower layer; this ADR does
not promote it into a RuntimeModule.

Two runtime integrations share one composed module only when they are cohesive
across all four axes:

1. **App composition and lifecycle** — they are enabled, initialized,
   resolved, and shut down as one optional application part.
2. **Durable state and scope** — they share one state owner and the same
   world-scoped or global lifetime.
3. **Dependencies and commit target** — they require the same services and
   job/cancellation boundary, and commit into the same authoritative target.
4. **Published state and consumer reactions** — they publish state with the
   same meaning to the same consumers, and those consumers require the same
   standing follow-through.

A separate module is required when any of these objective split triggers
applies:

- the app must be able to compose, replace, or remove one integration without
  the other;
- their durable state has different ownership or world/global lifetime;
- they require distinct services, scheduling/job lifecycles, cancellation
  scopes, or authoritative commit targets; or
- their published state has different meaning, consumers, or standing
  reactions.

Algorithm family and result shape are evidence to examine, but neither is
necessary nor sufficient for grouping. Conversely, methods from different
algorithm families may share a module when the demonstrated composition and
consumer contract is identical.

Command, status, completion, and diagnostic records remain method-specific
until a second production integration demonstrates identical semantics for
identity, lifecycle, cancellation, commit, failure reporting, and consumer
meaning. At that point the shared portion may be extracted as the smallest
plain-data contract needed by both callers. Matching field types or a common
word such as "labels" does not establish that contract.

Potential future methods are conditional probes, not predetermined placements:

- **DBSCAN** may share K-Means composition only if its noise-label semantics,
  state lifetime, commit target, and downstream reactions prove identical;
  otherwise it splits or retains method-specific records.
- **Hierarchical clustering** may share a flat-label consumer path, but
  persistent hierarchy/dendrogram state or cut-level reactions are an
  objective split trigger.
- **GMM** remains planned lower-layer geometry numerics for CPD and CLOP
  consumers today. A future runtime integration is classified from its actual
  posterior/state consumers and commit contract, not from the
  clustering-family name.

This ADR governs logical responsibility grouping, not the C++ mechanism used
to compose it. It neither ratifies nor changes `IRuntimeModule`; the
`REVIEW-003` right-sizing audit may retain, replace, or remove that interface
while preserving this cohesion rule.

## Consequences

- Positive: future integrations have a repeatable grouping test grounded in
  composition, ownership, and consumers instead of a speculative taxonomy.
- Positive: method-specific semantics remain explicit, and a shared DTO cannot
  silently erase cancellation, commit, diagnostic, or reaction differences.
- Trade-off: the first two similar-looking integrations may carry small
  method-specific records until production evidence proves a common contract.
- Trade-off: module placement is decided when a real runtime integration is
  designed rather than preallocated on a roadmap.
- No engine, method, module, event, or interface change follows directly from
  this ADR.

## Alternatives Considered

- **Predeclare one RuntimeModule per algorithm family — rejected.** Family
  names do not establish shared lifecycle, state, commit, or consumers, and
  the repository has no second production clustering integration.
- **Group solely by result shape — rejected.** Equal-looking outputs can have
  different meaning and reactions; different-looking outputs can still belong
  to one cohesive app-composed responsibility.
- **Create a generic clustering completion record now — rejected.** K-Means is
  the only production caller, so no second case proves common semantics.
- **Freeze `IRuntimeModule` as part of the taxonomy — rejected.** The grouping
  decision is independent of its current implementation mechanism, whose
  retention belongs to `REVIEW-003`.

## Validation

- Documentation link and docs-sync checks cover this ADR and its canonical
  architecture references.
- Future runtime-integration reviews apply the four-axis cohesion test and
  record any split trigger in the owning task or ADR.
- A proposed shared command, status, completion, or diagnostic record must name
  the two production callers and demonstrate identical semantics across the
  criteria above.
