# METHOD-008 — Resolve `_example_vector_heat` method manifest placeholders

## Goal
- Either complete the method intake for `methods/geometry/_example_vector_heat/` (fill `authors`, `year`, `doi`) per `AGENTS.md` §6 step 1, or rename the directory to make its scaffold-only status unambiguous to the manifest validator.

## Non-goals
- Do not start a CPU reference backend, correctness tests, or benchmarks for the Vector Heat Method in this task. Implementation is owned by a future METHOD-* task gated on a real paper intake.
- Do not modify `tools/agents/validate_method_manifests.py`.
- Do not change `methods/_template/` or any other method package.

## Context
- Owning subsystem/layer: `methods/geometry/` package metadata; `tools/agents/validate_method_manifests.py` consumes the `method.yaml`.
- File: [`methods/geometry/_example_vector_heat/method.yaml`](../../../methods/geometry/_example_vector_heat/method.yaml).
- Today the manifest contains:
  - `paper.authors: "TODO"`
  - `paper.year: 0`
  - `paper.doi: "TODO"`
  - `paper.url: "https://example.invalid/vector-heat"`
- The validator currently passes (`Method manifest validation passed for 2 file(s).`) because the fields exist as strings; however, per `AGENTS.md` §6 step 1, method intake requires a defined paper contract before any further method work, and the directory name leading underscore (`_example_...`) implies scaffold status that is not encoded anywhere the validator inspects.
- The real "Vector Heat Method" paper (Sharp, Soliman, Crane — TOG 2019) is a legitimate intake candidate; this task explicitly does not commit to authoring it.

## Required changes
- [ ] Pick exactly one outcome and execute it:
  - [ ] **Outcome A (rename as scaffold):** Move `methods/geometry/_example_vector_heat/` to `methods/_template/_example_vector_heat/` (or `methods/_examples/_vector_heat/`, matching whichever convention already exists for non-real examples). Update `id:` to encode the scaffold status (e.g. `examples.vector_heat`). Update every cross-link to the old path.
  - [ ] **Outcome B (real intake):** Fill `paper.authors`, `paper.year`, `paper.doi`, and `paper.url` with the real Sharp/Soliman/Crane TOG 2019 values, drop the leading underscore from the directory name, and update `paper.md` to a real (non-placeholder) one-paragraph summary. Note: this outcome opens the door to a follow-up METHOD-* task for the reference backend, but does not require it in this commit.
- [ ] If Outcome A: update [`tools/agents/validate_method_manifests.py`](../../../tools/agents/validate_method_manifests.py) only if the rename moves the file outside its current scan root; otherwise leave it untouched.
- [ ] Update any cross-link from method docs, `docs/methods/`, or task files that reference the old path.

## Tests
- [ ] `python3 tools/agents/validate_method_manifests.py` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes (cross-links resolve after rename, if any).
- [ ] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.
- [ ] Default CPU gate remains green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Docs
- [ ] If Outcome A: update [`methods/README.md`](../../../methods/README.md) (or the closest current methods index) to record where scaffold-only examples live.
- [ ] If Outcome B: update [`methods/README.md`](../../../methods/README.md) to reflect that Vector Heat Method intake exists; add a one-line note pointing at the follow-up implementation task slot.

## Acceptance criteria
- [ ] No `"TODO"` strings remain in any `method.yaml` under `methods/geometry/`.
- [ ] No `year: 0` placeholder remains in any `method.yaml` under `methods/geometry/`.
- [ ] The directory name unambiguously communicates whether the package is a scaffold/template (Outcome A) or a real intake (Outcome B).
- [ ] `validate_method_manifests.py` continues to report 0 findings.

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
