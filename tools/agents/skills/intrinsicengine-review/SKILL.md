---
name: intrinsicengine-review
description: Pre-commit, pre-PR, and recurring review procedures for IntrinsicEngine. Bundles five checklists: the per-PR review checklist (scope, layering, tests, docs, maturity closure), the architecture review checklist (layering invariants, lifetime/ownership, concurrency, error handling for architecture-impacting changes), the clean-workshop drift/decomposition scorecard for boundary-touching changes, the weekly human-led audit of agent-authored commits (silent scope creep, decorative comments, premature abstraction, documented-but-not-tested claims, ceremony-without-shipped-value), and the recurring whole-tree repo-state drift audit. Use this skill whenever the user is about to commit, open a PR, finish a slice, ask "is this ready to merge", change a dependency boundary or module ownership, run the weekly agent-output audit or drift audit, or whenever pre-commit/pre-merge verification of any non-trivial change is needed in this repo.
---

# IntrinsicEngine Review Procedures

This skill bundles the IntrinsicEngine review procedures with progressively
wider scope. **Apply them in order of relevance to the change at hand** — most
PRs need only the per-PR checklist; architecture-impacting changes add the
architecture checklist (and usually the clean-workshop scorecard); the weekly
audit and the drift audit are separate human-led cadences.

| Procedure | When | Reference |
| --- | --- | --- |
| Per-PR review | Before every commit / PR for a non-trivial change | `references/review-checklist.md` |
| Architecture review | When changing dependency boundaries, module ownership, source layout, runtime wiring, or architecture docs | `references/architecture-review-checklist.md` |
| Clean-workshop scorecard | When a change touches a dependency boundary, adds a renderer subsystem/pass, changes RHI/platform/runtime wiring, closes a scaffold/parity task, or edits the layering allowlist | `references/clean-workshop-review.md` |
| Weekly agent-output audit | Once per week, human-led, covering ~1 week of agent-authored slices | `references/agent-output-review-checklist.md` |
| Repo-state drift audit | On demand or every 2–4 weeks, human-led, whole-tree state audit (inventory drift, stale markers, dead seams, cross-doc rot) | `references/drift-audit-checklist.md` |

---

## 1. Per-PR review checklist

Apply to **every** non-trivial change. Read
`references/review-checklist.md` for the full checklist with rationale; the
sections below summarise what to verify.

### Scope and ownership

- Change maps to exactly one task (unless batching is explicitly allowed).
- Owning subsystem/layer is identified.
- **Mechanical moves and semantic edits are not mixed** in the same commit.

### Maturity and closure

- For tasks closing to `tasks/done/`: the reached maturity level is named
  (`Scaffolded`, `CPUContracted`, `Operational`, `ParityProven`, `Retired` — see
  `intrinsicengine-task-workflow` for the taxonomy). State it in the task
  `Status` block, retirement commit message, or completion summary — pick one
  and use it consistently.
- If the closing task body uses "scaffold", "stub", "fail-closed", or "minimal"
  language, it either names a follow-up task ID for the next maturity level **or**
  records an explicit `Non-goals` line that pins the scaffold as the intended
  endpoint.
- For rendering, Vulkan, asset ingest, hot reload, pass command bodies, runtime
  composition, and legacy retirement tasks: an `Operational` claim cites the
  backend-labeled or integration-labeled run that **actually executed** (not
  just CPU contract coverage).

### Architecture and layering

- Dependency flow follows `AGENTS.md` invariants.
- No cross-layer convenience imports introduced.
- Runtime wiring remains in `runtime`.
- CMake `target_link_libraries(...)` edges between promoted targets treat the
  link as an architecture dependency, not a build-system convenience.
  `tools/repo/check_layering.py --root src --strict` covers both C++23 module
  imports and CMake link edges.

**Shader push-constant compatibility (Vulkan critical):** For any new or
modified pipeline whose pass body calls `cmd.PushConstants(&pc, sizeof(pc))`,
the selected vertex/fragment/compute shaders **must** declare a
`layout(push_constant) ...` block whose layout mirrors the pushed struct
byte-for-byte, and whose descriptor-set expectations match the pipeline layout.
The CPU/null contract gate only proves that the renderer issued a
`PushConstants` call — on a real Vulkan run a layout mismatch silently
reinterprets the bytes. **Never feed `RHI::GpuScenePushConstants` bytes into
the legacy `assets/shaders/surface.{vert,frag}` / `surface_gbuffer.frag` /
`shadow_depth.vert` pairs**; they declare `mat4 Model + uint64_t Ptr*` and
`set = 2/3` SSBOs and will misinterpret `SceneTableBDA` as `mat4 Model[0]`.
The GpuScene-aware shader inventory under `assets/shaders/forward/` and
`assets/shaders/deferred/` follows the `default_debug_*` template pattern; see
`src/graphics/renderer/README.md` "Shader push-constant compatibility policy".

### Testing

- Strongest relevant verification subset was run.
- Tests for behavior changes were added or updated.
- Test labels are correct: `unit`, `contract`, `integration`, `regression`,
  `gpu`, `vulkan`, `benchmark`, plus opt-in `slow`/`flaky-quarantine`.
- Focused build/test targets were run **before** broad or long-running targets.
- If `tools/ci/touched_scope.py` was used, its selected commands are recorded
  and any broad fallback/full-gate requirements are still addressed.
- Build trees used for evidence were confirmed current and compatible with
  C++23/toolchain requirements (highest complete Clang 20+ toolchain selected
  by the preset, or an explicitly verified equivalent).
- Current CTest output, not stale `LastTestsFailed.log` contents, was used to
  assess pass/fail state.
- Noisy command output was captured to a log and filtered with
  `set -o pipefail` so failures remain visible.

### Documentation and tasks

- Docs updated for structural/policy/behavior changes (see `intrinsicengine-docs-sync`).
- Links are updated and valid.
- Task records synchronized across `active`, `backlog`, `done`.

### CI and temporary shims

- Touched CI/workflow logic remains readable and maintainable.
- Any temporary shim is recorded with removal task ID and timeline
  (`AGENTS.md` §13).

---

## 2. Architecture review checklist

Trigger this **in addition to** the per-PR checklist when the change touches
any of: dependency boundaries, module ownership, source layout, runtime wiring,
or architecture docs. Read `references/architecture-review-checklist.md` for
the full version.

### Layering and ownership

- Owning layer/subsystem is explicit (`core`, `geometry`, `assets`, `ecs`,
  `physics`, `graphics/*`, `runtime`, `app`, `legacy`).
- New dependency edges are justified and align with `AGENTS.md` invariants.
- No lower layer imports higher layers.
- `runtime` remains the composition root; lower layers remain reusable.
- `tools/repo/check_layering.py --root src --strict` covers both C++23 module
  imports (`import Extrinsic.<Layer>.*;`) and CMake `target_link_libraries(...)`
  edges; new dependencies are visible to the strict run and either pass or
  carry a tracked allowlist entry with `task`/`expires` metadata.
- **Use the `knowledge-graph` MCP aid to scope the review.** When the server is
  available (see `intrinsicengine-core`), query it to map the changed modules'
  neighbors and the change's downstream blast radius, and to eyeball
  `violation`-tagged edges. Its edges cover **C++23 module `import` only** — it
  does not parse `#include`, so for any unit still using headers the module graph
  understates the blast radius; cover those with source search and the strict
  `check_layering.py` run. **Confirm every layering finding with the strict
  `check_layering.py` run above**, which remains the sole gate and covers both
  `import` and `#include` edges. The graph speeds discovery; it never substitutes
  for the gate.

### Lifetime and resource ownership

- Ownership model is explicit (value, handle, unique owner, borrowed view).
- Cross-system references avoid hidden lifetime coupling.
- Temporary compatibility shims are tracked with a removal task ID.

### Concurrency and synchronization

- Threading model is explicit for touched paths (main thread, task graph,
  async workers, GPU queue).
- Shared mutable state has a clear synchronization strategy.
- No blocking behavior introduced on hot paths without justification.

### Error handling and diagnostics

- Failure states are explicit and propagated deterministically.
- New failure/error modes include actionable diagnostics.
- Fallback behavior is documented when applicable.

### Architecture vocabulary and the deletion test

Use this vocabulary in every architecture-impacting suggestion or finding —
consistent language is the point. Don't drift into "component", "service",
"helper", or "boundary".

- **Module** — anything with an interface and an implementation. In this repo
  that is typically a `.cppm` interface unit plus its partitions and
  implementation TUs, but it can also mean a free function, a class, a
  promoted CMake target, or a layer subdir.
- **Interface** — everything a caller must know to use the module: exported
  names, types, invariants, error/return contract, threading expectations,
  ordering, and any required composition. Not just the function signatures.
- **Implementation** — the code behind the interface.
- **Depth** — leverage at the interface: a lot of behavior behind a small
  interface. **Deep** = high leverage. **Shallow** = interface nearly as
  complex as the implementation.
- **Seam** — where an interface lives; a place behavior can be altered
  without editing in place. Use this instead of "boundary".
- **Adapter** — a concrete thing that satisfies an interface at a seam (e.g.
  the Vulkan RHI adapter satisfying the RHI interface).
- **Leverage** — what callers get from depth.
- **Locality** — what maintainers get from depth: change, bugs, knowledge
  concentrated in one place.

Heuristics to apply during architecture review:

- **Deletion test.** For any module you suspect is shallow, imagine deleting
  it. If complexity vanishes, it was a pass-through and should be deleted or
  inlined. If complexity reappears across N callers (or N backends), it was
  earning its keep. A "yes, concentrates" is the signal for a deep module
  worth keeping.
- **The interface is the test surface.** If a behavior cannot be exercised
  through the public interface, either the interface is wrong or the test is
  wrong. Adding a private test seam is usually the wrong fix.
- **One adapter = hypothetical seam. Two adapters = real seam.** Don't
  introduce an interface for a single implementation unless a second backend
  is concretely planned in `tasks/active/` or `tasks/backlog/`. The
  reference vs. optimized vs. GPU backend split under `methods/` is the
  archetypal real seam.
- **Naming.** Identifiers should use the engine's domain vocabulary (layer
  names, RHI terminology, maturity levels, backend identities). Drift into
  generic terms ("Manager", "Handler", "Helper", "Util") is a smell.

These heuristics also feed the weekly audit row 3 (premature abstraction) —
a single-adapter interface is almost always premature.

### Maturity on architecture-impacting closures

- The reached maturity level (see `intrinsicengine-task-workflow`) is stated
  for any architecture-impacting closure so future agents do not misread
  "foundation exists" as "capability shipped".

### CI and PR contract gates

- PR template sections are completed (`Summary`, `Type`, `Layering`, `Tests`,
  `Docs`, `Performance`, `Benchmarking`, `Agent self-review`, `Temporary shims`).
- Workflow impacts are reviewed for readability and trigger correctness.
- Strict validators remain green (or warning-mode exceptions are documented).

---

## 3. Weekly agent-output audit

This is an **additive** human-led cadence; it does **not** gate PR merges and
does **not** impose per-commit reviewer load. Each weekly sweep covers ~1 week
of slices and runs in ≤ 60 minutes. The reviewer rotates — see
`intrinsicengine-core` (`references/roles.md`).

Findings are saved under `docs/reports/<YYYY-MM-DD>-agent-output-audit.md`.

### How to run

1. Pick a window (calendar week, or coherent task sequence). Record the commit
   range:
   ```bash
   git log --pretty=format:"%h %ad %s" --date=short --no-merges \
       --since=<START> --until=<END>
   ```

2. For each row in `references/agent-output-review-checklist.md` (rows 1–9),
   decide `pass`, `findings`, or `not-applicable`. `not-applicable` is allowed
   only when the window contains no commits that could surface the failure
   mode (e.g., docs-only windows skip rows 4 and 8).

3. For every `findings` row, link an existing follow-up task or open a new
   backlog task and reference the audit report from that task.

4. Record total elapsed time at the bottom of the audit report so future
   cadences can calibrate against the ≤ 60-minute target. If a sweep exceeds
   the budget, **tighten the checklist** rather than relax the cadence.

5. Missing a week is not a failure — the next reviewer extends the window.

### Failure modes audited (rows 1–9)

| Row | Failure mode |
| --- | --- |
| 1 | Silent scope creep — touches files outside the task's required-changes |
| 2 | Decorative comments and docstrings on internal symbols |
| 3 | Premature abstraction — interfaces/wrappers/factories for a single implementation |
| 4 | Documented-but-not-tested — README/architecture claim with no matching test |
| 5 | Defensive validation at internal boundaries (reviewer heuristic) |
| 6 | Untracked compatibility shims — no `tasks/active/` exception record or removal task ID |
| 7 | Ceremony without shipped value — a whole window of task/docs ceremony with no behavior change |
| 8 | Half-finished implementations — new seam never exercised by a test or call site |
| 9 | Aspirational documentation without `(planned)` marker |

Read `references/agent-output-review-checklist.md` for each row's full
definition, how-to-find-it commands, and how-to-fix-it remediation.

---

## References

- `references/review-checklist.md` — per-PR checklist with full rationale; the
  authoritative pre-commit/pre-PR gate.
- `references/architecture-review-checklist.md` — checklist for architecture-impacting
  changes; layered on top of per-PR.
- `references/agent-output-review-checklist.md` — weekly human-led audit with
  9 numbered failure-mode rows; additive cadence, not a per-PR gate.
- `references/clean-workshop-review.md` — drift/decomposition scorecard for
  boundary-touching changes; triggered from the per-PR checklist.
- `references/drift-audit-checklist.md` — recurring whole-tree repo-state
  drift audit; state-scoped, additive to the weekly sweep.
