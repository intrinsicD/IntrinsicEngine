---
name: intrinsicengine-reuse
description: Find existing IntrinsicEngine implementations and shared helpers before adding code, or audit and consolidate repeated mechanisms. Use for canonical-owner discovery, reuse searches, duplicate implementation cleanup, and preventing copy-pasted processing or UI workflows.
---

# IntrinsicEngine Reuse

Find the existing owner by behavior, check its contract, and reuse it when it
fits. This skill owns the discovery/consolidation procedure; `AGENTS.md` owns
layering, authorization and verification. Use `intrinsicengine-right-sizing`
when the question is whether an abstraction should exist at all.

Examples:

- `$intrinsicengine-reuse find the existing way to publish an undoable scalar property`
- `$intrinsicengine-reuse audit processing methods for repeated GPU query handling`
- `$intrinsicengine-reuse consolidate the matching density and spacing helpers`

## Find before implementing

For a new implementation, run a focused search before adding a non-trivial
mechanism, helper or file. A find/review request stays read-only. An audit adds
candidate ranking to discovery; implement only when the user authorizes fixes.
An implementation request already authorizes necessary reuse within its scope.

1. Express the operation as behavior plus constraints, not the proposed class
   name: e.g. "publish a named same-domain scalar with stale-source undo guards."
   Search synonyms too: publish/write/apply, revision/generation/stale,
   nearest/kNN/neighborhood, preview/validate/config, display/show/appearance.
2. Read only the relevant rows of [owner routes](references/owner-routes.md).
   They are search seeds, not an exhaustive registry or proof of correctness.
   Start in the owning layer from `AGENTS.md`; then search its consumers.
   Include private headers and implementation units, not just exported modules.

   ```bash
   rg --files src/runtime src/app tests | rg 'Point|Property|Spatial|Panel'
   rg -n 'CapturePointScalarField|ObserveGeometryProperty|PublishPointScalarField' src tests
   ```

   Substitute terms/roots for the actual task. Use the generated
   `docs/api/generated/module_inventory.md` for public module ownership,
   `docs/architecture/contract-catalog.yaml` for applicable contracts, and
   layer READMEs for navigation. Search tasks/reviews for prior decisions only
   when needed; a historical proposal is not current implementation evidence.
   The optional knowledge graph omits headers and never replaces source search.
3. Read the candidate definition, its real callers and the relevant behavior
   tests. Follow the call into the mechanism: a forwarding facade is not its
   owner. If a wrapper adds a lifetime, validation or layering guarantee,
   preserve that guarantee. Search symbol references before changing visibility
   or deleting a helper; includes, CMake sources and test consumers also count.
4. Compare the contracts below. Choose **reuse**, **extend the existing owner**,
   **share a proven common mechanism**, or **keep separate**. A matching name or
   matching lines is insufficient. If nothing fits, record searched terms and
   the missing capability; add the smallest implementation at the proper owner.
5. Leave a compact reuse decision in the existing task/review or response:
   intent; owner path and symbol; caller/test evidence; chosen action; retained
   semantic differences. Include a negative search when relevant. Do not create
   a permanent report for every small edit or claim an exhaustive search.

## Contract fit

Check the axes relevant to the operation and state any mismatch:

- **Data:** element/property domain, type, units/coordinate space, cardinality,
  deleted-row mapping, topology, input/output aliasing and unrelated data.
- **Query/numerics:** metric, radius versus kNN, width floors, self/coincident/tie
  rules, ordering, truncation, precision and failure behavior.
- **Ownership:** borrowed versus owned storage, thread/frame lifetime, revision
  guards, attachment epochs, cancellation and terminal result delivery.
- **Publication/control:** history and dirty-notification order; typed config,
  preview/apply and UI parity; requested/actual backend and fallback semantics.

A real parameter such as neighbor width can express a common mechanism. Do not
introduce method-name switches or a growing set of policy flags to hide different
contracts. Radius support and fixed-width kNN remain separate. Public enums or
result types need not be merged just because private mechanisms can be shared.

## Consolidate an authorized scope

- Rank candidates by verified repetition, number of current callers, likelihood
  of fixes drifting, and the tests available. Start with one proven family.
  Record an exact baseline before edits, including existing uncommitted work.
- Prefer a present owner and compiled free functions with ordinary records.
  Share only the matching mechanism; keep distinct algorithms and their typed
  adapters explicit. Introduce a new file only for a concrete ownership or
  compilation reason. Do not put a large lifecycle template in a public module.
- Replace the callers and delete the duplicate bodies in the same slice. Count
  all affected production files/physical lines before and after, including new
  helpers, adapters and build entries; explain increases. Moving code, splitting
  files and compression of formatting are not simplification. No arbitrary line
  caps or source-shape tests to force a favorable count.
- Verify behavior through the existing public entry points. Add only missing
  coverage exposed by the shared mechanism. Preserve layer boundaries and run
  the applicable repository checks; actual GPU execution is required when a GPU
  continuation changes. Do not infer a compile/runtime speedup from fewer lines.
- Review the fixed final diff. Use Claude or another external reviewer only when
  requested/authorized; give it the bounded diff and relevant contracts, and keep
  it read-only while one writer owns the checkout. Check every finding against
  source and fix valid findings before repeating affected verification.
- Keep discovery current: update the relevant owner route or existing owner
  documentation when a listed helper moves or is replaced. Verify paths and
  symbols in touched rows, and correct or remove stale pointers. Do not add a
  second symbol database, mandatory search server, or exhaustive helper catalog.
  Record broader candidates without silently widening the authorized work.
