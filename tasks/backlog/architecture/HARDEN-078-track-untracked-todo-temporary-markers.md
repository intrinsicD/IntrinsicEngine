---
id: HARDEN-078
theme: F
depends_on: []
---
# HARDEN-078 — Track or resolve untracked TODO / temporary markers in promoted src

## Goal
- Give an owning task ID (or resolve) the two untracked migration markers in
  promoted (non-legacy) `src/` surfaced by the 2026-06-06 drift audit
  ([`docs/reports/2026-06-06-drift-audit.md`](../../../docs/reports/2026-06-06-drift-audit.md)
  Row 7), so no promoted source carries a bare `TODO:` or a "temporary"
  migration bridge without a tracked removal owner per `AGENTS.md` §13.

## Non-goals
- Do not change the behavior of `Core.Filesystem` or `Runtime.Engine`; this is
  a marker-hygiene task, not a feature or migration task.
- Do not delete the `GetStreamingGraph()` bridge here — only record its removal
  owner; the actual removal belongs to the streaming-migration follow-up that
  this task names.
- Do not sweep `src/legacy/` (legacy retirement is owned by `LEGACY-001..010`).
- Do not mix this with unrelated audit-row findings.

## Context
- Owner/layer: `core` (`src/core/Core.Filesystem.cppm`) and `runtime`
  (`src/runtime/Runtime.Engine.cppm`, `src/runtime/README.md`). Each edit is
  independent.
- Source of the finding: the calibration drift audit
  ([`REVIEW-002`](../../done/REVIEW-002-recurring-drift-and-inconsistency-audit.md), report
  [`2026-06-06-drift-audit.md`](../../../docs/reports/2026-06-06-drift-audit.md))
  Row 7 (untracked TODO/shim drift).
- The two markers:
  1. `src/core/Core.Filesystem.cppm:16` — a commented-out
     `import Extrinsic.Core.CallbackRegistry;` carrying a bare
     `//TODO:` design question (with a "CallbaclRegistry" typo) about whether
     the filewatcher should use the callback registry, own one, or use
     dependency injection. No task ID.
  2. `src/runtime/Runtime.Engine.cppm:258` — `GetStreamingGraph()` is
     `[[deprecated("Use Runtime.StreamingExecutor integration; TaskGraph bridge
     is temporary.")]]`, and `src/runtime/README.md` calls it a "temporary
     compatibility bridge ... while migration is in progress." Neither names a
     removal task ID, so the §13 "temporary exception needs a removal owner"
     rule is unmet.

## Required changes
- [ ] For `Core.Filesystem.cppm`: either (a) make the filewatcher/callback
      design decision and remove the dead commented import + TODO, or (b) if the
      decision is deferred, convert the bare `//TODO:` into a `// TODO(<TASK-ID>)`
      form that names an owning backlog task (open that task if none fits) and
      fix the "CallbaclRegistry" typo.
- [ ] For the `GetStreamingGraph()` deprecation: open (or name an existing)
      streaming-migration removal task and reference its ID in both the
      `[[deprecated("...")]]` message and the `src/runtime/README.md`
      "Streaming integration" note, so the temporary bridge has a tracked
      removal owner per `AGENTS.md` §13.
- [ ] Re-run the Row 7 grep from the drift-audit checklist and confirm both
      markers now either are gone or carry a task ID.

## Tests
- [ ] No behavior change, so no new tests. The default CPU gate
      (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`)
      stays green for touched scope.
- [ ] `git grep -nE '(TODO|FIXME|XXX|HACK)([^A-Za-z0-9]|$)' -- 'src/core/**' 'src/runtime/**'`
      returns only task-ID-tracked markers for the two files above.

## Docs
- [ ] `src/runtime/README.md` "Streaming integration" note references the
      removal task ID if option (b)/bridge-tracking is chosen.
- [ ] No other docs change unless the design decision changes a public surface.

## Acceptance criteria
- [ ] `src/core/Core.Filesystem.cppm` carries no bare `//TODO:` (either resolved
      or task-ID-tracked); the typo is fixed if the line survives.
- [ ] `Runtime.Engine.cppm::GetStreamingGraph()` deprecation and the runtime
      README both name a removal task ID, or the bridge is removed by the named
      migration task (out of scope here).
- [ ] Default CPU gate green for touched scope.

## Verification
```bash
git grep -nE '(TODO|FIXME|XXX|HACK)([^A-Za-z0-9]|$)' -- 'src/core/**' 'src/runtime/**'
git grep -nE '\b(shim|backcompat|temporary)\b' -- 'src/runtime/Runtime.Engine.cppm' 'src/runtime/README.md'
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Removing the `GetStreamingGraph()` bridge in this task (record the owner; the
  migration task removes it).
- Changing filewatcher/callback behavior beyond resolving the dead TODO.
- Touching `src/legacy/`.
- Mixing this with unrelated drift-audit findings.

## Maturity
- Target: `Retired` (pure marker hygiene; no maturity progression on
  `Core.Filesystem` or `Runtime.Engine`).
