# METHOD-008 — Resolve `_example_vector_heat` method manifest placeholders

## Status

- Status: done (retired 2026-06-06) via **Outcome A** (rename as scaffold).
- Completed: 2026-06-06.
- Commit: this commit relocates the example and retires the task.
- What landed: `methods/geometry/_example_vector_heat/` was moved to
  `methods/_examples/vector_heat/` (a new `_examples/` location that makes the
  non-real / structure-example status unambiguous), the misleading paper
  placeholders (`authors: "TODO"`, `year: 0`, `doi: "TODO"`,
  `example.invalid/vector-heat`) were replaced with explicit
  structure-example values, the package README + `methods/README.md` layout +
  the new `methods/_examples/README.md` document the convention, and the stale
  prose path in `METHODS-001` was updated. `methods/geometry/` now holds no
  placeholder manifest.
- Adaptation: the task's Outcome-A suggestion to set `id: examples.vector_heat`
  is **not viable** — `validate_method_manifests.py --strict` requires a
  `geometry.`/`rendering.`/`physics.` id prefix and modifying that validator is
  a non-goal. The `id` therefore stays `geometry.example_vector_heat` (already
  encodes "example"); the unambiguous status comes from the `_examples/`
  location, the README, the manifest name, and `known_limitations`.
- Verification: `validate_method_manifests.py` (default) and
  `--root methods --strict` both pass (3 files); the
  `! grep -RInE '"TODO"|year:\s*0\b' methods/geometry/` sanity is clean (and the
  moved example is clean too); `check_doc_links` and `validate_tasks --strict`
  pass. No C++/test source changed, so the default CPU gate is unaffected
  (last green at 2736/2738 under HARDEN-073, the 2 failures being the unrelated
  benchmark-smoke Not-Run).

## Goal
- Either complete the method intake for `methods/geometry/_example_vector_heat/` (fill `authors`, `year`, `doi`) per `AGENTS.md` §6 step 1, or rename the directory to make its scaffold-only status unambiguous to the manifest validator.

## Non-goals
- Do not start a CPU reference backend, correctness tests, or benchmarks for the Vector Heat Method in this task. Implementation is owned by a future METHOD-* task gated on a real paper intake.
- Do not modify `tools/agents/validate_method_manifests.py`.
- Do not change `methods/_template/` or any other method package.

## Context
- Owning subsystem/layer: `methods/geometry/` package metadata; `tools/agents/validate_method_manifests.py` consumes the `method.yaml`.
- File: [`methods/geometry/_example_vector_heat/method.yaml`](../../methods/_examples/vector_heat/method.yaml).
- Today the manifest contains:
  - `paper.authors: "TODO"`
  - `paper.year: 0`
  - `paper.doi: "TODO"`
  - `paper.url: "https://example.invalid/vector-heat"`
- The validator currently passes (`Method manifest validation passed for 2 file(s).`) because the fields exist as strings; however, per `AGENTS.md` §6 step 1, method intake requires a defined paper contract before any further method work, and the directory name leading underscore (`_example_...`) implies scaffold status that is not encoded anywhere the validator inspects.
- The real "Vector Heat Method" paper (Sharp, Soliman, Crane — TOG 2019) is a legitimate intake candidate; this task explicitly does not commit to authoring it.

## Required changes
- [x] Pick exactly one outcome and execute it:
  - [x] **Outcome A (rename as scaffold):** Move `methods/geometry/_example_vector_heat/` to `methods/_template/_example_vector_heat/` (or `methods/_examples/_vector_heat/`, matching whichever convention already exists for non-real examples). Update `id:` to encode the scaffold status (e.g. `examples.vector_heat`). Update every cross-link to the old path.
  - [x] **Outcome B (real intake):** NOT CHOSEN — Outcome A executed instead, because the package is a non-real structure example, and asserting a real DOI/metadata would misrepresent it. (Original option: fill real Sharp/Soliman/Crane TOG 2019 values, drop the underscore, real `paper.md`.)
- [x] If Outcome A: update [`tools/agents/validate_method_manifests.py`](../../tools/agents/validate_method_manifests.py) only if the rename moves the file outside its current scan root — left untouched: the validator `rglob`s `methods/**/method.yaml`, so `methods/_examples/vector_heat/` stays in scope.
- [x] Update any cross-link from method docs, `docs/methods/`, or task files that reference the old path.

## Tests
- [x] `python3 tools/agents/validate_method_manifests.py` passes.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes (cross-links resolve after rename, if any).
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.
- [x] Default CPU gate remains green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Docs
- [x] If Outcome A: update [`methods/README.md`](../../methods/README.md) (or the closest current methods index) to record where scaffold-only examples live.
- [x] If Outcome B: update [`methods/README.md`](../../methods/README.md) to reflect that Vector Heat Method intake exists — n/a, Outcome A chosen (no real intake created).

## Acceptance criteria
- [x] No `"TODO"` strings remain in any `method.yaml` under `methods/geometry/`.
- [x] No `year: 0` placeholder remains in any `method.yaml` under `methods/geometry/`.
- [x] The directory name unambiguously communicates whether the package is a scaffold/template (Outcome A) or a real intake (Outcome B).
- [x] `validate_method_manifests.py` continues to report 0 findings.

## Verification
```bash
python3 tools/agents/validate_method_manifests.py
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/validate_tasks.py --root tasks --strict

# Sanity: no TODO placeholders remain in geometry method manifests.
! grep -RInE '"TODO"|year:\s*0\b' methods/geometry/
```

## Forbidden changes
- Implementing a CPU reference backend, tests, or benchmarks for the Vector Heat Method in this commit (that is a separate METHOD-* task).
- Touching `methods/_template/` semantics.
- Loosening `tools/agents/validate_method_manifests.py` to accept placeholder values.
- Mixing this metadata fix with any unrelated method package edits.
