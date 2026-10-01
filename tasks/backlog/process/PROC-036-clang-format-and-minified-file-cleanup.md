---
id: PROC-036
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: mechanical formatting work; the diff must be whitespace-only (verified by a token-equivalence check), evidence is that check, the build and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. A formatter configuration and format-only commits change no module surface, binding, publication, config or agent/UI control, and no catalog contract owns formatting.
---
# PROC-036 — Add a `.clang-format` and remove the minified-style outliers

## Goal
Give the repository a committed formatter configuration that matches its majority style, and bring
the minified-style files back to that style, without a semantic change and without making
`git blame` or concurrent branches unusable.

## Context
Source: 2026-10-01 duplication/consistency audit (finding 2.6), re-verified at `665c693dd`.

- There is no `.clang-format` or `.editorconfig` at the repository root, and no formatting rule in
  `AGENTS.md` or `docs/`. The environment used to write this note has no `clang-format` binary, so the
  config below is a measured proposal, not a validated one; CI uses clang 20.
- Majority style (938 `src` C++ files, measured on the audit tree): Allman braces (about 29,100
  own-line `{` against about 1,400 end-of-line after `)`); 4-space indent; code indented inside
  `namespace`; `if (` 18,500 against `if(` 714; `for (` 4,160 against `for(` 120; references
  attached to the type (`T& x` 12,000 against `T &x` 625) and pointers likewise (`T* x` 1,865
  against `T *x` 237); lines over 120 columns are about 0.9% (2,934 of 322,934), over 160 columns 453.
  1,211 lines contain tabs; check whether they are string literals before choosing `UseTab`.
- Minified outliers, counted as `if(` / `if (` (some also have lines over 160 columns):
  `Runtime.ClusteringGpuState.cpp` 60/0, `Runtime.ClusteringGpuBackend.cpp` 16/0,
  `Runtime.GeometryProcessingOperations.Keypoints.cpp` 61/19, `...DensityWeights.cpp` 30/4,
  `Runtime.PointScalarTransaction.cpp` 62/28 (one `Current()` predicate over 300 columns),
  `...Descriptors.cpp` 33/20, `Runtime.PointNeighborhoodConfigCodecs.cpp` 27/14,
  `Geometry.PointCloud.Features.cpp` 49/40, `Graphics.PointScalarAnalysis.cpp` 9/2,
  `Graphics.OutlierAnalysis.cpp` 9/2. Also mixed: `...Outliers.cpp` 62/69 and `...Bilateral.cpp`
  31/39. The Sandbox files are a separate case: `Sandbox.MeshProcessingPanels.cpp` has 56 lines over
  160 columns and many one-line multi-statement blocks, and `Sandbox.DomainPanels.cpp` indents with
  2 spaces where every other file uses 4. Shaders (for example `kmeans_update.comp`) use the same
  compressed style; `clang-format` is not applied to GLSL here, so shaders are out of scope.
- Several of these files are being edited by open work
  ([RUNTIME-311](../runtime/RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md),
  [RUNTIME-313](../runtime/RUNTIME-313-queued-editor-job-setup-and-completion-helper.md),
  [RUNTIME-314](../runtime/RUNTIME-314-reuse-existing-processing-helpers.md), UI-071/UI-072).
  Reformatting under them creates conflicts; land the format-only commit for each file when no branch
  holds it, or immediately before the semantic task starts.

## Decision needed (operator): how to apply the formatter
Options, to be chosen before the config is enforced anywhere:
1. **Incremental only.** Commit the config; developers and agents run `git clang-format` on their own
   changed lines; optionally add a CI check restricted to changed lines. No mass reformat. Cheapest
   and conflict-free, but long-lived minified files stay minified until touched.
2. **Per-file-family format-only commits.** Option 1 plus one format-only commit per file family (the
   ten outliers above first), never mixed with semantic changes (AGENTS.md §5), each verified
   token-identical, with the commit SHAs recorded in `.git-blame-ignore-revs`.
3. **One dedicated whole-tree reformat commit** plus `.git-blame-ignore-revs`. Cleanest end state, but
   touches about 938 files and conflicts with every open branch, worktree and agent session; needs a
   quiet window and rebase instructions.

The slices below assume option 2 unless the operator selects another; the minified-file cleanup the
operator asked for is the same under options 2 and 3.

## Slices
- [ ] Choose the option above and record it in this task's log.
- [ ] Add `.clang-format` derived from the measured style (proposal: `BasedOnStyle: LLVM`,
      `IndentWidth: 4`, `BreakBeforeBraces: Allman`, `NamespaceIndentation: All`,
      `PointerAlignment: Left`, `ColumnLimit: 120`, `SpaceBeforeParens: ControlStatements`,
      `AccessModifierOffset: -4`). Validate it with clang-format 20: run `--dry-run` over a sample of
      well-formed files (for example `Graphics.MaterialSystem.cpp`) and tune options until the number
      of changed lines on already-conforming files is near zero; record the measured diff size.
      Add `.editorconfig` only if it earns its lines.
- [ ] Document the rule in one short paragraph of the repository conventions docs (not a new file),
      and in `AGENTS.md` only if option 1 or 2 makes it an enforced rule.
- [ ] Reformat the ten minified files, then `...Outliers.cpp`, `...Bilateral.cpp` and the
      `Sandbox.DomainPanels.cpp` indentation, with format-only commits; prove each is
      whitespace/line-break-only by comparing the token stream (or the compiled object) before and
      after.
- [ ] Optional, only if the operator selects it: a CI step checking changed lines.

## Acceptance criteria
- [ ] `.clang-format` is committed, validated with clang-format 20, and its measured diff size on conforming files is recorded.
- [ ] The ten minified files no longer contain `if(` or multi-statement one-liners or lines over the column limit; each reformat commit is semantically empty and verified.
- [ ] The chosen application option, the blame-ignore revision list (if any), and how contributors apply the formatter are documented.
- [ ] No semantic change is mixed into a format commit; build and default CPU gate pass.

## Verification
```bash
clang-format --version
clang-format --dry-run --Werror src/graphics/renderer/Graphics.MaterialSystem.cpp
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
