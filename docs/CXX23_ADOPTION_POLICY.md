# IntrinsicEngine C++23 Adoption Policy

This document closes the architecture TODO on aligning day-to-day code with the repository's stated C++23 direction.

## 1) `std::expected` monadic operations

### Goal
Prefer explicit, linear error pipelines over nested `if (!x)` blocks in multi-stage fallible workflows.

### Required usage
Use monadic chaining (`.and_then(...)`, `.transform(...)`, `.or_else(...)`) when **all** of these are true:

1. The flow has **3+ fallible stages**.
2. Each stage consumes the prior stage's successful value.
3. Error forwarding is unchanged (or only enriched with additional context).

Typical targets:
- Import pipelines (`read -> decode -> build GPU payload -> register asset`).
- Asset export pipelines (`gather -> serialize -> write`).
- Runtime initialization sequences where every stage returns `std::expected`.

### Acceptable non-monadic style
A direct `if (!result) return std::unexpected(...)` style remains acceptable when:

- The flow has only one or two fallible calls.
- Branching logic dominates (format switches, policy-specific fallback paths).
- Performance-sensitive code benefits from explicit fast-path branching and clearer profiling markers.

### Error typing rule
Do not widen error types opportunistically in the middle of a chain. Keep a stable domain error (`AssetError`, `ErrorCode`, etc.) and convert once at module boundaries when needed.

## 2) Explicit object parameters (deducing `this`)

### Goal
Use explicit object parameters where they improve API symmetry and avoid const/non-const duplication.

### Recommended usage
Adopt explicit object parameters for:

- Stateless utility/member-style algorithms that differ only by cv/ref qualifiers.
- Fluent APIs where `this` forwarding correctness matters and can be encoded once.
- Small value-types in Core/Geometry where preserving inlining and readability is straightforward.

### Avoid usage
Avoid explicit object parameters when:

- It obscures a hot-path class API that is already clear and stable.
- Virtual interfaces are involved (prefer conventional member signatures).
- Team readability suffers versus equivalent conventional overloads.

## 3) Rollout plan

1. New code in import/export and asset ingestion paths should follow the monadic rule above.
2. Existing code should be migrated opportunistically during touch-up PRs; no large churn-only refactor.
3. Each migration PR should include one focused test update to preserve behavior.

## 4) Acceptance checklist for reviewers

- [ ] Multi-stage fallible flow uses monadic operations (or has a clear reason not to).
- [ ] Error domain remains stable through the pipeline.
- [ ] No readability regression in hot-path runtime code.
- [ ] Tests still validate success + representative failure cases.
