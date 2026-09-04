---
id: BUG-164
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Build-cache key policy for the CI-007 ccache launcher. No engine layer, geometry domain, property publication, method integration, or control-surface contract is touched; the fix lives in cmake/Dependencies.cmake and its regression coverage."
---
# BUG-164 — ccache serves stale objects when a macro changes only imported module BMIs

## Goal
- Make the CI-007 ccache launcher miss whenever a compile definition or flag
  changes the content of an imported module BMI, even if the importing TU's
  own preprocessed text is unchanged.

## Non-goals
- No change to which lanes use ccache (`pr-fast` only in hosted CI; local
  trees by default).
- No switch to ccache direct or depend mode without the module-safety
  evidence CI-007 recorded.

## Context
- Symptom: while landing `BUG-157`, `GLM_FORCE_XYZW_ONLY` was added as a
  directory-scope definition. In `build/ci-clang20` (ccache launcher active,
  `--fresh` configure) `src/graphics/renderer/Passes/Pass.Selection.Outline.cpp`
  "compiled" although it still used `selection.OutlineColor.r/.g/.b/.a` on a
  `glm::vec4` that no longer has those members; a direct
  `clang++-20 -fsyntax-only` invocation of the identical command line reports
  eight `no member named 'r' in 'glm::vec<4, float>'` errors, and the
  ccache-disabled clean rebuild fails the TU as expected.
- Mechanism: the launcher runs ccache with `CCACHE_NODIRECT=1` (preprocessor
  mode) plus `CCACHE_EXTRAFILES` pointing at a digest of every `.cppm`
  interface source. In preprocessor mode ccache derives the key from the
  preprocessed TU text and deliberately excludes `-D`/`-I` options, so a
  definition that only alters headers included by *other* modules' global
  module fragments (glm in `Extrinsic.RHI.Types`, `Extrinsic.Graphics.*`)
  changes the imported BMIs but neither the TU's preprocessed text nor the
  interface-source digest. The stale object is then reused.
- Expected behavior: any input that changes an imported BMI (compile
  definitions, flags, headers pulled into a global module fragment) must
  change the cache key for every importer.
- Impact: local incremental and `pr-fast` builds can report success or
  failure for objects compiled against outdated module interfaces; a green
  `pr-fast` after a header-level configuration change is not trustworthy. The
  hosted `full-cpu`, sanitizer, and Vulkan lanes do not install ccache and are
  unaffected.

## Required changes
- [ ] Extend the cache-key digest with the compile definitions/flags of the
      module libraries (or with the BMI file hashes ninja already tracks), so
      the key changes whenever an importer's BMI inputs change.
- [ ] Keep the launcher module-safe: interfaces still pass through, and the
      digest stays deterministic and configure-time.

## Tests
- [ ] Add a tooling regression that builds a two-module fixture with the
      launcher, changes a definition affecting only the imported module's
      GMF header, and proves the importer is recompiled (ccache miss).

## Docs
- [ ] Update the CI-007 notes in `tests/README.md`/`docs/` that describe the
      digest policy.

## Acceptance criteria
- [ ] The `Pass.Selection.Outline.cpp` scenario (definition change without
      any `.cppm` edit) recompiles every importer under the launcher.
- [ ] `pr-fast` warm-cache evidence remains within the CI-007 budget.

## Verification
```bash
cmake --preset ci-fast
cmake --build --preset ci-fast --target ExtrinsicGraphics
cmake --preset ci-fast -DCMAKE_CXX_FLAGS=-DINTRINSIC_BUG164_PROBE=1
cmake --build --preset ci-fast --target ExtrinsicGraphics -- -d explain 2>&1 | grep -c "Pass.Selection.Outline.cpp.o"
python3 tests/regression/tooling/Test.CcacheModuleDigest.py --build-dir build/ci-fast
```

## Forbidden changes
- Disabling ccache globally instead of fixing the key.
- Treating a `pr-fast` pass as evidence for a header-configuration change
  until this is fixed.
