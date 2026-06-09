# Review Checklist

Use this checklist before commit and PR creation.

## Scope and ownership

- [ ] Change maps to exactly one task (unless batching explicitly allowed).
- [ ] Owning subsystem/layer is identified.
- [ ] Mechanical moves and semantic edits are not mixed.

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
- [ ] Test labels/category are correct (`unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`).
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
- [ ] Links are updated and valid.
- [ ] Task records synchronized (`active`, `backlog`, `done` as appropriate).

## CI and temporary shims

- [ ] Touched CI/workflow logic remains readable and maintainable.
- [ ] Any temporary shim is recorded in tracker with removal task and timeline.


Related: `docs/agent/architecture-review-checklist.md`,
`docs/agent/clean-workshop-review.md` (drift/decomposition scorecard),
`docs/agent/drift-audit-checklist.md` (whole-tree state audit).
