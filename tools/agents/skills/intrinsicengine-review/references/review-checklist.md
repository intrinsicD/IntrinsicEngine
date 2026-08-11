# Review Checklist

Use this checklist before commit and PR creation.

## Scope and ownership

- [ ] Change maps to exactly one task (unless batching explicitly allowed).
- [ ] Owning subsystem/layer is identified.
- [ ] Mechanical moves and semantic edits are not mixed.
- [ ] Enrolled task profile matches the risk trigger; owner/branch/worktree
      metadata mirrors a live Git-common-dir claim.
- [ ] Parallel writers used separate worktrees; explicit path claims do not
      overlap.
- [ ] A claimed non-micro task has a live repository work graph; its current
      node and queued notes are visible, its exact claim generation and
      writer-frozen review digest still match, any completed final binding
      matches the current surface, and graph status is not cited as a
      substitute for task evidence or independent review.
- [ ] The task declares every applicable ID from
      `docs/architecture/contract-catalog.yaml`, considering owning and
      consuming layers, data/publication domains, and config/agent/runtime/UI
      surfaces; a justified-empty review is credible.
- [ ] Task wording did not narrow a canonical contract. Any intentional
      contract change updates its canonical source, catalog routing metadata,
      and executable proof in this change.

## Evidence and review custody

- [ ] `standard` and higher completion has a generated, current
      `tasks/evidence/<TASK-ID>/report.yaml`, successful required command
      receipts, and every acceptance criterion disposed.
- [ ] Skipped checks have bounded reasons; failed required commands and
      residual blocking findings are not presented as completion.
- [ ] `high-risk` and higher work has a final handoff plus accepted independent
      review bound to the exact final commit/content digest (or the derived
      `worktree:<content-digest>` identity for an explicitly dirty draft).
      Append-time and validation-time checks both enforce the binding.
      Self-review remains provisional and labels are not treated as
      authentication.
- [ ] `claim-grade` work has a frozen protocol, sealed non-overwriting run,
      visible cell failures/missing results, portable bundle, and independent
      recomputation audit; the completion validator proves the run bindings
      equal the protocol rather than merely naming hash-valid files.
- [ ] `protected` work has result-free prospective review, separate launch
      authorization, current protocol/implementation digests, and one-shot
      attempt consumption with a terminal `failed` or `completed` record.

## Maturity and closure

- [ ] For tasks closing to `tasks/done/`: the reached maturity level is
      named (see [`task-maturity.md`](../../../../../docs/agent/task-maturity.md): `Scaffolded`,
      `CPUContracted`, `Operational`, `ParityProven`, `Retired`). State it
      in the task `Status` block, retirement commit message, or completion
      summary — pick one and use it consistently.
- [ ] If the closing task body uses "scaffold", "stub", "fail-closed", or
      "minimal" language, it either names a follow-up task ID for the next
      maturity level or records an explicit `Non-goals` line that pins the
      scaffold as the intended endpoint.
- [ ] For rendering, Vulkan, asset ingest, hot reload, pass command bodies,
      runtime composition, and legacy retirement tasks: an `Operational`
      claim cites the backend-labeled or integration-labeled run that
      actually executed (not just CPU contract coverage).

## Architecture and layering

- [ ] Dependency flow follows `AGENTS.md` invariants.
- [ ] No cross-layer convenience imports introduced.
- [ ] Runtime wiring remains in `runtime`.
- [ ] New abstraction surface (interfaces, forwarding facades,
      registration/scheduling frameworks, event/command hops, dependency
      structs) passes the justified-complexity keep-list in
      `tools/agents/skills/intrinsicengine-right-sizing/SKILL.md`; anything
      flagged as ceremony carries a right-sizing plan or follow-up task.
- [ ] Data-driven additions use plain structs/free functions; any new
      interface/factory/wrapper/backend seam has a present second caller, a
      layering boundary, a test double, or a config/UI/agent variant axis
      justifying it (`AGENTS.md` §5, P1).
- [ ] New engine-tunable state is reachable from config files and an agent/CLI
      path (not UI-only) and round-trips through the config lane with a
      side-effect-free preview/validate step (`AGENTS.md` §5, P3).
- [ ] Frame/composition changes are expressed as recipe data and stay
      introspectable; the main loop remains an ordered, readable phase list
      (`AGENTS.md` §5, P5).
- [ ] If the change touches a dependency boundary, a renderer subsystem/pass,
      RHI/platform/runtime wiring, a scaffold/parity closure, or a layering
      allowlist entry, run the clean-workshop scorecard
      (`docs/agent/clean-workshop-review.md`, or
      `tools/ci/run_clean_workshop_review.sh . --strict`) and record findings as
      follow-up task IDs.
- [ ] CMake `target_link_libraries(...)` edges between promoted targets
      treat the link as an architecture dependency, not a build-system
      convenience. `tools/repo/check_layering.py --root src --strict`
      covers both C++23 module imports and CMake link edges.
- [ ] **Shader push-constant compatibility.** For any new or modified
      pipeline whose pass body calls `cmd.PushConstants(&pc, sizeof(pc))`,
      the selected vertex/fragment/compute shaders MUST declare a
      `layout(push_constant) ...` block whose layout mirrors the pushed
      struct byte-for-byte (and whose descriptor-set expectations match
      the pipeline layout). The CPU/null contract gate only proves that
      the renderer issued a `PushConstants` call; on a real Vulkan run a
      layout mismatch silently reinterprets the bytes. Concretely, never
      feed `RHI::GpuScenePushConstants` bytes into the legacy
      `assets/shaders/surface.{vert,frag}` / `surface_gbuffer.frag` /
      `shadow_depth.vert` pairs (they declare `mat4 Model + uint64_t Ptr*`
      and `set = 2/3` SSBOs). See
      `src/graphics/renderer/README.md` ("Shader push-constant
      compatibility policy") for the GpuScene-aware shader inventory under
      `assets/shaders/forward/` and `assets/shaders/deferred/` and the
      `default_debug_*` template pattern.

## Testing

- [ ] Strongest relevant verification subset was run.
- [ ] Tests for behavior changes were added or updated.
- [ ] Test labels/category are correct (categories: `unit`, `contract`, `integration`, `regression`, `benchmark`, `slo`; capability labels `gpu`/`vulkan`/`glfw` and opt-in labels `slow`/`flaky-quarantine` per `AGENTS.md` §7 and `tests/README.md`).
- [ ] Focused build/test targets were run before broad or long-running targets.
- [ ] If `tools/ci/touched_scope.py` was used, its selected commands are recorded and any broad fallback/full-gate requirements are still addressed.
- [ ] Build trees used for evidence were confirmed current and compatible with repository C++23/toolchain requirements.
- [ ] Current CTest output, not stale `LastTestsFailed.log` contents, was used to assess pass/fail state.
- [ ] Noisy command output was captured to a log and filtered with `set -o pipefail` so failures remain visible.

## Performance and benchmarking

- [ ] No unsubstantiated performance claims.
- [ ] Benchmarks/manifests updated where required.
- [ ] Smoke vs heavy benchmark intent is explicit.

## Documentation and tasks

- [ ] Docs updated for structural/policy/behavior changes.
- [ ] Every new or materially changed `.cppm` or header has a short, meaningful
      file synopsis; declaration comments are limited to mandatory non-obvious
      contracts, and implementation rationale is placed with implementation.
- [ ] Changed comments describe current invariants rather than task history,
      and changed README files are concise current-state navigation without
      changelogs, retired-slice narratives, or future plans
      ([source documentation policy](../../../../../docs/agent/source-documentation-policy.md)).
- [ ] Links are updated and valid.
- [ ] Task records synchronized (`active`, `backlog`, `done` as appropriate).
- [ ] Every declared contract is disposed by executable evidence or a named
      follow-up; method work includes the complete engine-integration matrix.

## CI and temporary shims

- [ ] Touched CI/workflow logic remains readable and maintainable.
- [ ] Any temporary shim is recorded in tracker with removal task and timeline.


Related: `docs/agent/architecture-review-checklist.md`,
`docs/agent/workflow-evidence.md`,
`docs/agent/clean-workshop-review.md` (drift/decomposition scorecard),
`docs/agent/drift-audit-checklist.md` (whole-tree state audit).
