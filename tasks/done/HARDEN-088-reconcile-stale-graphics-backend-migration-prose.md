---
id: HARDEN-088
theme: F
depends_on:
  - GRAPHICS-018
  - GRAPHICS-076E
  - GRAPHICS-076F
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-harden088"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T15:30:28Z"
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this task reconciles comments and current-state documentation with existing graphics-backend behavior and changes no reusable API, backend selection, data-domain, publication, or control-surface contract."
---
# HARDEN-088 — Reconcile stale graphics-backend migration prose

## Status

- Completed and retired on 2026-08-06. Promoted backend comments and READMEs
  now describe the reserved frame-sampled descriptor range as current
  backend-local policy, consistently reserve slots `0..5`, and start real
  bindless leases at `6`. The stale retired-task TODO/temporary ownership and
  obsolete future-Vulkan Null prose are gone. A machine check proves every C++
  diff line is comment-only; graphics-labeled CPU coverage passed 851/851 and
  strict clean-workshop, task, docs-sync/link, and marker scans pass.
- Completion commit: this retirement commit.
- In progress on `main`: correct the identified Vulkan/Null current-state
  comments and READMEs without modifying executable C++.

## Goal

- Remove stale TODO, temporary-migration, and future-backend claims from the
  promoted graphics backends while documenting the load-bearing behavior that
  actually exists.

## Non-goals

- No RHI, renderer, descriptor, sampler, pipeline, Null, or Vulkan behavior
  change.
- No redesign or deletion of `BindFrameSampledTextureAt(...)` or the reserved
  descriptor range; its multiple pass consumers and volatile backend boundary
  satisfy the right-sizing keep-list.
- No new validator for prose vocabulary.

## Context

- The rejected `REVIEW-003` baseline found `TODO(GRAPHICS-018)` on a public
  Vulkan device surface even though `GRAPHICS-018` is retired and the listed
  helper families are implemented.
- Vulkan source and README prose still call the frame-sampled descriptor path a
  temporary `GRAPHICS-076E/076F` bridge although both owners are retired and
  the path now serves postprocess, debug view, present, selection outline, and
  object-space normal-bake dilation.
- `Backends.Vulkan.Descriptors` reserves elements `0..5` and starts real leases
  at `6`, while one Vulkan README paragraph still says leases start at `4`.
- The Null backend module and README describe a future sibling Vulkan backend
  and preserved TODO markers that do not exist. Promoted Vulkan already lives
  at `src/graphics/vulkan/`, outside the Null renderer-backend directory.

## Right-sizing decision

- **Element:** slot-explicit frame-sampled descriptor binding and its reserved
  backend-local range.
- **Keep reason:** present volatile-backend boundary plus five concrete pass /
  bake consumers; removing the seam would redistribute backend descriptor
  knowledge into renderer callers.
- **Simpler correction:** describe the range as current backend policy, remove
  retired migration-owner language, and keep behavior byte-for-byte unchanged.
- **Reintroduction trigger:** not applicable; this task removes stale status
  prose rather than a production surface.

## Required changes

- [x] Replace the retired `GRAPHICS-018` TODO with factual current-state API
      documentation or remove it when the declarations are self-explanatory.
- [x] Rewrite the `GRAPHICS-076E/076F` temporary-bridge comments as current
      Vulkan descriptor-binding invariants without a retired removal owner.
- [x] Make every slot-range statement agree on reserved elements `0..5` and
      first real bindless lease `6`.
- [x] Rewrite the Null backend module and README as a current deterministic
      backend fixture; remove the nonexistent TODO/future-Vulkan claim.
- [x] Scan the touched backend surfaces for another stale migration-status
      marker before closing.

## Tests

- [x] Existing graphics contract tests pass; no behavior-specific test is
      added for prose-only corrections.
- [x] Strict docs, layering, task, and clean-workshop checks pass.

## Docs

- [x] Synchronize `src/graphics/vulkan/README.md`, the Null backend README, and
      any canonical graphics prose touched by the corrected current state.
- [x] Refresh task/session/retirement records.

## Acceptance criteria

- [x] No promoted graphics source or current-state backend README calls the
      retained frame-sampled path temporary or assigns it to a retired removal
      owner.
- [x] No promoted backend TODO names retired `GRAPHICS-018` work.
- [x] Null and Vulkan current-state descriptions match the existing tree and
      descriptor constants.
- [x] `git diff` contains no executable C++ change.

## Verification

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
git diff --check
```

## Forbidden changes

- Changing descriptor indices, bindless allocation, shader bindings, command
  recording, backend selection, or operational-gate behavior.
- Replacing retired task references with a speculative future task.
- Broad graphics documentation cleanup unrelated to the identified stale
  migration claims.
