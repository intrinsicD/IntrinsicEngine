---
id: BUG-164
theme: G
depends_on: []
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "interactive pair session; evidence is the reviewed diff and CI"
owner: Codex
branch: codex/bug-164-ccache-bmi-staleness
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-04T10:29:45+02:00"
contract_schema: 1
contracts: []
contract_review: "Build-cache key policy for the CI-007 ccache launcher. No engine layer, geometry domain, property publication, method integration, or control-surface contract is touched; the fix lives in cmake/Dependencies.cmake and its regression coverage."
---
# BUG-164 — ccache serves stale objects when a macro changes only imported module BMIs

## Goal
- Make the CI-007 ccache launcher miss whenever a compile definition or flag
  changes the content of an imported module BMI, even if the importing TU's
  own preprocessed text is unchanged.

## Status
- In progress. The dependency-local semantic sidecar passes the hermetic
  interface, directory/target-definition, target-option, and GMF-header cases
  plus an 18/18 real-core warm rebuild and the exact graphics macro repro.
  Hosted `pr-fast` warm-budget evidence remains open.

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
- [x] Bind importer keys to the semantic preprocessing inputs of their module
      dependencies, including directory-scope definitions such as
      `GLM_FORCE_XYZW_ONLY`.
- [x] Keep module-interface compilation as compiler pass-through and emit a
      deterministic sidecar before each module compile.
- [x] Cover target-private module definitions/flags and GMF-header content
      changes without hashing the whole CMake cache, Ninja graph, or source
      tree.

## Slice plan
- **Slice A (complete).** Extend the existing hermetic fixture with a macro
  used only by an imported module's global-module-fragment header; reproduce
  the stale importer hit, then add the narrowest configure-time key input that
  forces changed-macro misses while retaining unchanged warm hits.
- **Slice B (implemented; hosted evidence pending).** Exercise target-private
  module definitions/options and a GMF-header content edit. Each module compile
  emits a semantic sidecar from its exact `CXXDependInfo.json` entry, source and
  project/build-local `.ddi.d` inputs, and direct dependency sidecars. Importers
  hash only the sidecars named by their `.ddi`/`.modmap` requirements.

## Right-sizing
- Extend the existing `tools/ci/ccache_ci.py` boundary with one launcher
  subcommand instead of adding a target-property registry, header crawler, or
  second wrapper file. CMake's existing scanner metadata already identifies
  exact module sources, textual inputs, compile context, and direct imports;
  the launcher reduces those inputs to one small sidecar per module.
- Blast radius is the existing C/C++ compiler-launch path plus its hermetic
  tooling tests. Raw PCM bytes are deliberately excluded because a real Clang
  module produced nondeterministic bytes and signatures across clean rebuilds.
  A new abstraction is warranted only if another compiler or module metadata
  format must be supported later.

## Tests
- [x] Add a tooling regression that builds a module/importer fixture with the
      launcher, changes a definition affecting only the imported module's
      GMF header, and proves the importer is recompiled (ccache miss).
- [x] Cover target-private definition, target-private option, textual
      GMF-header, deterministic sidecar, fail-closed metadata, and unchanged
      real-target warm-hit behavior.

## Docs
- [x] Update the CI-007 notes in `tests/README.md`/`docs/` that describe the
      digest policy.

## Acceptance criteria
- [x] The `Pass.Selection.Outline.cpp` scenario (definition change without
      any `.cppm` edit) recompiles every importer under the launcher.
- [ ] `pr-fast` warm-cache evidence remains within the CI-007 budget.

## Verification
```bash
python3 tests/regression/tooling/Test.CcacheModuleInvalidationProbe.py -v
python3 tests/regression/tooling/Test.CcacheWorkflow.py -v
cmake --preset ci-fast --fresh
cmake --build --preset ci-fast --target IntrinsicPrFastTests
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
```

## Observation ledger
- 2026-09-04 — Baseline, `python3
  tests/regression/tooling/Test.CcacheModuleInvalidationProbe.py -v` with
  ccache 4.9.1 and Clang 23: predicted existing interface-edit coverage would
  pass; observed 4/4 pass.
- 2026-09-04 — Unfixed macro fixture, same command: predicted the `.cppm`-only
  fingerprint would reuse stale consumers after `PROBE_GMF_VALUE=11→29`;
  observed output `29` instead of clean `47`, with `Probe.cpp` and `main.cpp`
  both reporting `preprocessed_cache_hit` and zero cache misses.
- 2026-09-04 — Context-fingerprint fixture: predicted the macro edit would
  miss while the unchanged rebuild retained hits; observed 5/5 regression
  tests pass, two unchanged hits, and two misses for each changed-input case.
- 2026-09-04 — Production configure probe: the first schema validation exposed
  a leading CMake-list semicolon before the context record; after switching to
  `string(CONCAT)`, `ccache_ci.py check-config` passed. A macro flag changed the
  digest from `a2acbf43…` to `87603d4d…`, and an identical reconfigure retained
  `87603d4d…`.
- 2026-09-04 — Final verification: the real-ccache `ci-fast` target built with
  608 cold misses and zero ccache errors; its 3,940-test unit/contract selector
  passed with two expected skips. The canonical `ci` `IntrinsicTests` build
  completed 2,313/2,313 edges and the CPU-supported selector passed 4,263/4,263
  with six expected capability/lifecycle skips.
- 2026-09-04 — Slice B unfixed target-private fixture: predicted the Slice A
  project-wide context would miss `target_compile_definitions(probe PRIVATE
  …)`; observed cached output `47` instead of clean `52`, with `Probe.cpp` and
  `main.cpp` both reporting `preprocessed_cache_hit` and zero cache misses.
- 2026-09-04 — PCM-stability probe: the minimal fixture reproduced SHA-256
  `1657c83a…` for `Probe.pcm`, but the production `Extrinsic.Core.Filesystem`
  PCM and its embedded Clang signature changed on every clean rebuild. Raw PCM
  keying yielded 17 hits/1 miss and was rejected.
- 2026-09-04 — Semantic-sidecar fixture: all five changed-input cases produced
  two importer misses and matched their clean no-ccache output; the unchanged
  rebuild produced two hits. Unit coverage also proves raw PCM byte changes do
  not perturb the sidecar while a GMF-header content change does.
- 2026-09-04 — Production core backtest: after a cold 18-miss population,
  cleaning and rebuilding `ExtrinsicCore` produced 18 preprocessed hits, zero
  misses, zero ccache errors, and 45 expected module-interface pass-throughs.
- 2026-09-04 — Production graph backtest: a broad `IntrinsicPrFastTests` build
  exposed identical duplicate `-fmodule-file` mappings in CMake's generated
  module map. The launcher now accepts duplicates only when they resolve to
  the same PCM, rejects conflicts in unit coverage, and completed all
  1,384/1,384 build edges.
- 2026-09-04 — Exact graphics backtest: in an isolated ccache store seeded
  only from the no-macro state, adding `GLM_FORCE_XYZW_ONLY` without a fresh
  configure changed the outline module sidecar from `ac638da2…` to
  `bf1915bc…`; `Pass.Selection.Outline.cpp` reported
  `preprocessed_cache_miss`, while its `.cppm` remained the expected ccache
  pass-through. The temporary top-level definition edit was restored exactly.
- 2026-09-04 — Semantic-sidecar final local gate: the final `ci-fast`
  configuration passed launcher/schema validation and built the selected
  `IntrinsicPrFastTests` closure through 1,651/1,651 edges. The independent
  no-ccache `ci` `IntrinsicTests` build succeeded, and the CPU-supported CTest
  selector passed 4,263/4,263 with six expected skips.

## Forbidden changes
- Disabling ccache globally instead of fixing the key.
- Treating a `pr-fast` pass as evidence for a header-configuration change
  until this is fixed.
