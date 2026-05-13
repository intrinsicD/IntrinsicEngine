# Geometry API Style and Numeric Policy

This document is the canonical policy for new `src/geometry` APIs. It turns the
current findings from the [`src/geometry` gap analysis](../reviews/2026-05-12-src-geometry-gap-analysis.md)
into rules that future geometry tasks can cite without refactoring existing code
opportunistically.

The layer contract remains `geometry -> core` only. Geometry code must not import
or depend on assets, ECS, graphics, RHI, runtime, platform, or app ownership.

## Scope

Applies to new or materially changed public geometry APIs, modules, diagnostics,
and method-integration seams. Existing APIs may keep their current shape until a
dedicated compatibility or migration task changes them.

Do not mix mechanical renames, module moves, or namespace normalization with
semantic algorithm changes. When existing APIs need alignment, create a focused
follow-up task that records compatibility expectations and inventory updates.

## Module, file, and namespace style

- Prefer one narrow module per coherent geometry concept or algorithm family.
- Name module interface files as `Geometry.<Concept>[.<Subconcept>].cppm` and use
  an exported module name that matches the file stem.
- Place public symbols under `Geometry::<Concept>` or another explicitly
  documented namespace that matches the module concept.
- Use the broad `Geometry` umbrella only for stable, commonly composed APIs.
  Advanced or experimental numerical modules may remain narrow imports until
  their public role is intentionally expanded.
- Do not add broad umbrella exports merely for convenience; callers should import
  the least-specific module set they need.
- Keep implementation helpers non-exported where possible. Exported `Internal`
  namespaces are compatibility debt unless the helper types have stable semantics
  and tests.

When a new task must deviate from these rules to preserve compatibility, the task
record should name the deviation and include a removal or normalization follow-up.

## Public state and mutability

- Plain data records may expose public fields when they are simple values with no
  invariants beyond their documented types.
- Owning containers and acceleration structures should expose cheap const access
  through spans or noun accessors and reserve mutation for explicit operations.
- Mutable borrowed views must document the source storage they mutate and the
  lifetime requirements of that borrow.
- Algorithms should request the least structured domain they need. Use point-cloud
  views for point samples, graph views for traversal, and mesh views for face or
  topological editing.
- Use explicit hard-copy conversions when topology/cardinality changes, when
  independent lifetime is required, or when attribute layout conversion is
  necessary. Those conversions should return diagnostics rather than silently
  dropping data.

## Naming and count terminology

- Prefer `PascalCase` for public functions and methods, matching the dominant
  promoted geometry style.
- Prefer noun accessors such as `Nodes()` or `Elements()` for cheap views. Use
  `Get*` only when matching an existing local API family or when the operation is
  not a trivial accessor.
- Use `Size()` for storage slots, including deleted or inactive slots when a
  container has sparse handles.
- Use `Count()` for live logical elements.
- Use `Capacity()` only for reserved storage capacity.
- Name conversion APIs to reveal ownership: use `View`/`Borrow` for no-copy
  adaptation, `To*`/`From*` for owning copies, and `Consume` only for intentional
  ownership transfer.

## Failure reporting and diagnostics

New public APIs should preserve enough information for deterministic tests,
method comparisons, and paper-result diagnostics:

- Use `Core::Expected<T>` for operations that can fail with caller-visible error
  conditions and need a result value.
- Use structured result records for algorithms that can partially succeed,
  iterate, converge, reject preconditions, or produce diagnostics such as counts,
  residuals, thresholds, or topology changes.
- Use status or error enums only when the result type is otherwise obvious and the
  enum values are documented with stable meanings.
- Use `std::optional<T>` only for trivial lookup-style APIs where absence is the
  sole expected outcome and no additional diagnostic is useful.
- Avoid `bool` failure returns for new algorithms unless the operation is a local
  predicate and cannot usefully report why it failed.
- Assertions may guard programmer errors and internal invariants, but malformed
  input, numerical singularity, unsupported topology, and non-convergence should
  be reported through public diagnostics.

Diagnostics should be deterministic: avoid locale-dependent formatting, hidden
randomness, or dependence on traversal order unless that order is specified.

## Numeric policy

- Keep `glm::vec*` and `float`-oriented storage for public geometry positions,
  directions, and renderer-facing data unless a task explicitly defines another
  representation.
- Use `double` internally for numerical kernels where conditioning, accumulation,
  residuals, or predicate stability matters.
- Expose tolerances as named parameters or policy records when callers may need to
  reproduce results across datasets or scales.
- Choose tolerances from documented scale assumptions. Prefer scale-normalized
  thresholds for algorithms that operate on arbitrary model units.
- Report degeneracy, rejected elements, singular pivots, residuals, and iteration
  limits through diagnostics when they affect results.
- Randomized algorithms must accept deterministic seed/state input and document
  whether outputs are stable across platforms.
- Future robust-predicate work should provide orientation, incircle/insphere,
  intersection, barycentric, and epsilon/scale-aware comparison utilities before
  expanding boolean, remeshing, arrangement, or reconstruction kernels.

The preferred numerical direction is a hybrid GLM + Eigen3 policy: GLM remains the
public geometry storage vocabulary, while Eigen may be introduced behind
geometry-owned adapters for CPU linear-algebra kernels when a task adds that
infrastructure. Eigen types should not leak through broad geometry APIs unless a
future task deliberately defines a public advanced numerical surface.

## `Geometry.LinearSolver` policy

`Geometry.LinearSolver` is currently a narrow public module interface listed in
`src/geometry/CMakeLists.txt` and the generated module inventory, but it is not
re-exported by the broad `Geometry` umbrella. Treat it as an advanced narrow
import for the current small fixed-size solver helper, not as the canonical public
solver infrastructure.

Future solver work should happen under a dedicated task such as `GEOM-008`. That
work may either promote a documented solver API and umbrella-export decision or
hide/replace the current helper behind implementation details. Do not broaden or
remove the module as part of unrelated algorithm changes.

## Compatibility and migration

Existing geometry inconsistencies are compatibility debt, not permission for
opportunistic cleanup. Follow these rules:

- Record style migrations as separate tasks when they touch public names,
  modules, namespaces, or generated inventories.
- Keep stale comments and local naming cleanups mechanical and isolated.
- Do not change behavior while moving files or renaming modules.
- Update `docs/api/generated/module_inventory.md` whenever module surfaces change.
- Update architecture and task docs in the same change when a policy decision or
  dependency boundary changes.
