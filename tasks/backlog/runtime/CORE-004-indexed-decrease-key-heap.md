---
id: CORE-004
theme: none
depends_on: []
maturity_target: CPUContracted
---

# CORE-004 — Indexed decrease-key min-heap container and Dijkstra adoption

## Goal

- Add a reusable core indexed binary min-heap (`Core.IndexedHeap`, namespace `Extrinsic::Core`) that tracks each element's position so `decrease-key` and `remove` are O(log n), exposing `push` / `pop` / `top` / `contains` / `decrease-key` / `remove`.
- Make the container generic over key/value with a caller-supplied comparator and a deterministic, total tie-break so equal-priority elements pop in a stable, reproducible order.
- Adopt the indexed heap inside the `Geometry.Graph.ShortestPath` Dijkstra implementation, replacing the `std::priority_queue` + lazy-insert / stale-entry-skip pattern with a true decrease-key while preserving identical shortest-path results (distances and predecessors).

## Non-goals

- No Fibonacci heap, pairing heap, or other amortized-O(1)-decrease-key structure; a plain indexed binary heap is the only data structure in scope.
- No GPU backend, no compute-shader path, and no concurrency/thread-safety for the heap.
- No UI, visualization, or editor changes.
- No change to the public shortest-path API in `Geometry.Graph.ShortestPath.cppm` (`Dijkstra`, `ExtractPathGraph`, `DijkstraParams`, `ShortestPathResult` signatures and field semantics stay exactly as they are).
- No removal or behavioral change to the existing `Core.BoundedHeap` (KNN bounded heap) or the versioned edge heap; this is an additive container.

## Context

- The core layer currently ships only a fixed-capacity KNN bounded heap (`src/core/Core.BoundedHeap.cppm`, module `Extrinsic.Core.BoundedHeap`, namespace `Extrinsic::Core`). There is no general indexed heap with decrease-key.
- Geometry's Dijkstra (`src/geometry/Geometry.Graph.ShortestPath.cpp`, `DijkstraCommon`) uses `std::priority_queue<FrontNode, std::vector<FrontNode>, FrontNodeGreater>` with lazy insertion: a relaxed vertex is pushed again and stale pops are discarded via the `node.Distance > result.Distances[node.Vertex]` and `settled[...]` guards. This grows the queue with stale entries instead of updating an existing key in place.
- `FrontNodeGreater` already encodes the deterministic ordering Dijkstra relies on: order by `Distance` ascending, break ties by `VertexHandle.Index` ascending. The indexed heap must be able to reproduce exactly this ordering so adoption is behavior-preserving.
- This is a general-purpose container, so it must live in the core layer (`core -> nothing`). Geometry consumes core; geometry must not host this container. Layering: `geometry -> core` only; `src/geometry/*` must not import assets/runtime/graphics/rhi/ecs/app, and core must not gain any new dependency.
- The container is consumed by `Geometry.Graph.ShortestPath`; both the mesh-backed and graph-backed `Dijkstra` overloads route through the single shared `DijkstraCommon` template, so a single adoption point covers both.

## Slice plan

- [ ] Slice A (maturity `CPUContracted`): land the `Core.IndexedHeap` container plus its unit tests in the core layer. Defers all geometry adoption. Closes only after the container is contracted (fail-closed degenerate behavior, deterministic tie-break, invariant + reference-PQ parity tests green).
- [ ] Slice B (maturity `CPUContracted`): adopt the indexed heap inside `DijkstraCommon` (true decrease-key) and assert path/distance/predecessor parity against the prior `std::priority_queue` implementation on random graphs. No public API change.

## Required changes

- [ ] Add `src/core/Core.IndexedHeap.cppm` exporting module `Extrinsic.Core.IndexedHeap` in namespace `Extrinsic::Core`. Declare an `IndexedHeap<Key, Value, Compare>` class template (default `Compare = std::less<Key>`) where:
  - `Value` is an externally meaningful identity (e.g. an integer index / handle) used as the stable lookup token for `contains` / `decrease-key` / `remove`; internal array positions for each `Value` are tracked in a position map so those operations are O(log n).
  - Tie-breaking is total and deterministic: when `Compare` reports two keys equivalent, ordering falls back to the `Value` token so pop order is reproducible run-to-run regardless of insertion order.
  - Because `IndexedHeap<Key, Value, Compare>` is a generic class template instantiated by downstream importers (e.g. the `Geometry.Graph.ShortestPath` comparator over `(Distance, VertexHandle.Index)`), its member definitions — including the non-trivial sift-up/sift-down and position-map bodies — must stay **visible to importers** as exported/inline template definitions in the `.cppm`. The C++23 module rule's "non-trivial bodies go in a `.cpp` implementation unit" carve-out does **not** apply here: templates that must be visible to instantiate stay in the interface. Do **not** move the generic heap's template bodies into a private `Core.IndexedHeap.cpp` (that would make them unavailable to downstream instantiations unless every `Key/Value/Compare` combination is explicitly instantiated). Keep them tidy via a `detail` namespace / inline template definitions in the interface rather than a separate implementation unit.
- [ ] Provide the operation surface: `Push(key, value)`, `Top()` (returns the current minimum key/value pair; precondition documented and fail-closed for empty), `TryTop(out)` / `Peek` style non-throwing query, `Pop()` / `TryPop(out)`, `Contains(value)`, `DecreaseKey(value, newKey)`, and `Remove(value)`. Define and document the contract for each, including the no-op vs. fail-closed result when `value` is absent or when `newKey` is not actually a decrease.
- [ ] Make every operation deterministic and fail-closed on degenerate input: empty-heap `Pop`/`Top` must report failure via the chosen return contract (e.g. `bool` / `std::optional`) rather than UB, assert, or returning garbage; `DecreaseKey`/`Remove` on an unknown `value` must report not-found rather than corrupting the heap; reject a `DecreaseKey` that would increase the key (return false / diagnostic, leave the heap unchanged). No asserts, no NaNs, no silent invariant breakage.
- [ ] Register the new interface unit in `src/core/CMakeLists.txt`: add `Core.IndexedHeap.cppm` to the `ExtrinsicCore` target `FILE_SET CXX_MODULES` list. The generic heap is header-style template-in-interface, so no `Core.IndexedHeap.cpp` implementation unit is expected; only add one if a strictly non-template helper (free function with no template parameters) is needed, and never for the template member bodies.
- [ ] (Slice B) In `src/geometry/Geometry.Graph.ShortestPath.cpp`, add `import Extrinsic.Core.IndexedHeap;` and replace the `std::priority_queue<FrontNode, std::vector<FrontNode>, FrontNodeGreater>` in `DijkstraCommon` with an `IndexedHeap` keyed by the `(Distance, VertexHandle.Index)` ordering currently encoded by `FrontNodeGreater`, using the vertex index as the `Value` token. On relaxation, call `DecreaseKey` for a vertex already in the frontier and `Push` for a new one, eliminating lazy re-insertion of stale entries.
- [ ] (Slice B) Preserve all `ShortestPathResult` diagnostics semantics: `QueuePushCount` continues to count frontier insertions (document whether decrease-key now counts as a push or as a separate update, and keep the meaning stable), and `SettledVertexCount`, `RelaxedEdgeCount`, `ReachedGoalCount`, `Converged`, `EarlyTerminated` keep identical values for identical inputs. Remove the now-unnecessary stale-entry skip (`node.Distance > result.Distances[...]`) only insofar as the indexed heap makes it dead; do not change the settled-budget, goal-stop, or empty-set semantics.
- [ ] (Slice B) Remove the `<queue>` include from `Geometry.Graph.ShortestPath.cpp` only after the priority queue is fully removed; do not leave both code paths in place.

## Tests

- [ ] Add `tests/unit/core/Test.Core.IndexedHeap.cpp` and register it in `tests/CMakeLists.txt` with `LABELS unit core` (mirror the existing `Test.Core.BoundedHeap.cpp` registration; do not introduce a new CTest label).
- [ ] Heap-invariant property test: after every `Push`, `Pop`, `DecreaseKey`, and `Remove`, the min-heap parent/child ordering holds and the internal position map agrees with the actual array positions for all live values.
- [ ] `DecreaseKey` correctness: decreasing a key re-orders the heap so the affected value surfaces at the correct rank; a subsequent `Top`/`Pop` sequence yields keys in non-decreasing order; an attempted increase is rejected and leaves the heap unchanged.
- [ ] `Remove` correctness: removing an arbitrary interior value keeps the invariant, updates `Contains`, and leaves the remaining pop order identical to the same heap built without that value.
- [ ] Reference-parity property test: run a randomized sequence of `Push` / `Pop` / `DecreaseKey` / `Remove` operations against a brute-force reference (e.g. `std::map`-backed or full-rescan priority model) and assert identical pop order, including deterministic tie-breaking on equal keys across repeated seeded runs.
- [ ] Degenerate / fail-closed cases: `Pop`/`Top` on an empty heap reports failure (no UB); single-element heap supports `Push` then `Pop`/`Remove`/`DecreaseKey` correctly; `DecreaseKey`/`Remove`/`Contains` on an absent value report not-found and do not mutate the heap.
- [ ] (Slice B) Add Dijkstra parity coverage in `tests/unit/geometry/Test_ShortestPath.cpp` (registered with `LABELS unit geometry`): on a corpus of seeded random graphs, the indexed-heap Dijkstra produces identical `Distances` and `Predecessors` (and identical `SettledVertexCount` / `Converged`) to the prior `std::priority_queue` reference for every source/target configuration covered (forward tree, reverse tree, source-to-target, multi-source, disconnected components).
- [ ] (Slice B) Parity test includes equal-edge-weight graphs that force tie-breaks, confirming predecessor selection matches the prior `FrontNodeGreater` index-ordered tie-break exactly.

## Docs

- [ ] Document `Core.IndexedHeap` in `src/core/README.md` alongside the existing `Core.BoundedHeap` entry: state its purpose (general indexed min-heap with O(log n) decrease-key/remove), the comparator + deterministic tie-break contract, and the fail-closed degenerate behavior.
- [ ] Regenerate the module inventory (`docs/api/generated/module_inventory.md`) via `tools/repo/generate_module_inventory.py` so the new `Extrinsic.Core.IndexedHeap` module is listed.
- [ ] (Slice B) Note in the shortest-path doc/comment surface that Dijkstra now uses a true decrease-key indexed heap and that results are unchanged versus the prior lazy-insert implementation; do not claim a performance improvement without a baseline comparison.

## Acceptance criteria

- [ ] `Core.IndexedHeap` exists in the core layer (`src/core/Core.IndexedHeap.cppm`, module `Extrinsic.Core.IndexedHeap`, namespace `Extrinsic::Core`), is registered in `src/core/CMakeLists.txt`, and `core` gains no new dependency.
- [ ] `Push`, `Pop`, `Top`, `Contains`, `DecreaseKey`, and `Remove` are all present, with `DecreaseKey` and `Remove` running in O(log n) via an internal position map (no O(n) scan to locate a value).
- [ ] All container unit tests pass, including the invariant property test, the reference-parity randomized test, the deterministic tie-break test, and the degenerate fail-closed cases.
- [ ] (Slice B) `DijkstraCommon` uses `IndexedHeap` with no remaining `std::priority_queue` and no `<queue>` include; the public shortest-path API in `Geometry.Graph.ShortestPath.cppm` is byte-for-byte unchanged.
- [ ] (Slice B) The Dijkstra parity test passes: identical `Distances` and `Predecessors` to the prior implementation across the seeded random-graph corpus, including tie-break cases.
- [ ] `check_layering.py --strict`, `check_test_layout.py --strict`, `check_doc_links.py`, and `check_task_policy.py --strict` all pass; the module inventory is regenerated in the same change.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'IndexedHeap|ShortestPath' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Do not place the indexed heap in the geometry layer or any layer other than core; the container must not pull in assets/runtime/graphics/rhi/ecs/app.
- Do not introduce any new dependency into the core layer (core -> nothing).
- Do not introduce renderer/runtime/ECS/assets/platform/app dependencies into the geometry shortest-path code as part of this work.
- Do not change the public shortest-path API (signatures or documented semantics of `Dijkstra`, `ExtractPathGraph`, `DijkstraParams`, `ShortestPathResult`).
- Do not alter or remove `Core.BoundedHeap` or the versioned edge heap.
- Do not introduce a new CTest label without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.
- Do not mix mechanical file moves with semantic refactors in the same change.
- Do not introduce unrelated feature work.
- Do not claim any performance improvement without a baseline comparison.

## Maturity

- Stop-state for both slices: `CPUContracted`. The container and the Dijkstra adoption are deterministic and fully contracted (fail-closed degenerate handling, deterministic tie-break, invariant + reference-parity + Dijkstra-parity tests green), but no benchmark-backed performance claim is made and no GPU/optimized backend is introduced. Do not pin a higher maturity (e.g. ParityProven against a perf baseline) within this task.
