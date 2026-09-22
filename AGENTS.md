# IntrinsicEngine Agent Contract

This is the authoritative repository contract. It supersedes conflicting policy
in `CLAUDE.md`, `.github/copilot-instructions.md`, and `.codex/config.yaml`.
Linked detailed requirements are mandatory when their scope applies; this file
wins in a conflict. Scoped reading changes neither those requirements nor gates.

## Agent skills

At session start, read this file and
`tools/agents/skills/intrinsicengine-core/SKILL.md`, then the task being continued.
Current, complete instructions already supplied in context count as read. Fetch
only missing, truncated, or changed sections; do not reload an unchanged source
or read both a canonical document and its generated skill reference.

The core skill routes to procedures for the touched scope. Read only the named
sections of expanded documents; the full session workflow is not a startup
prerequisite. Re-evaluate routes when scope changes. Skills live in
`tools/agents/skills/`, also surfaced through `.claude/skills/` and `.codex/skills/`;
agents without auto-discovery use that same entry point.

Edit canonical `docs/agent/*` sources, then run
`python3 tools/agents/sync_skills.py --write`; never hand-edit generated
`references/` mirrors. Read `tasks/SESSION-BRIEF.md` and `tasks/backlog/README.md`
only when selecting backlog work.

For tool execution, follow the workflow's
[output and wait procedure](docs/agent/prompt/prompt.md#tool-output-and-waits):
bound routine results, preserve full evidence, batch independent reads, and wait
on completion or meaningful changes without redundant polling.

## Shared optional session setup

Before provisioning a toolchain, dependencies, or the optional knowledge graph,
read [setup requirements](docs/agent/contract.md#shared-optional-session-setup).
Reuse `tools/setup/` entrypoints; setup never substitutes for preset verification.

## 1. Mission

Build a modular, high-performance, scientifically rigorous graphics and geometry
engine. Preserve buildability, testability, ownership, synchronized docs, and
reviewability. Framework24 user-facing feature/workflow convergence is the
standing P0 until `REVIEW-004` retires with an accepted verdict. Framework24 is a
behavioral baseline, not an implementation template. Follow its
[inventory and golden workflows](docs/product/framework24-convergence.md).
The operator may direct other work; record that direction and proceed.
When selecting work, read the [selection rules](docs/agent/contract.md#mission).
Selectable backends require real implementations and truthful requested/actual/
fallback reporting backed by evidence appropriate to their claims.

## 2. Non-negotiable architecture invariants

The following dependency boundaries are mandatory:

- `core` -> nothing.
- `geometry` -> `core`.
- `assets` -> `core`.
- `ecs` -> `core`; may use geometry handles/types only when explicitly required.
- `physics` -> `core`, `geometry`; owns simulation world/state, never live ECS/runtime/graphics/platform/app.
- `graphics/rhi` -> `core`.
- `graphics/assets` -> `core`, asset IDs (`Asset.Registry` types only), `graphics/rhi`; no live `AssetService` traffic.
- `graphics/vulkan` -> `core`, `graphics/rhi`, backend-local Vulkan dependencies (`Vulkan::Vulkan`, `volk`,
  `VulkanMemoryAllocator`, `glfw`); no ECS, runtime, or live asset-service knowledge, and no `Vk*` types through
  RHI/renderer APIs.
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views; **no live ECS knowledge**.
- `platform` -> `core`.
- `runtime` -> all lower layers; owns composition/wiring, including ECS-to-physics synchronization and physics-to-ECS writeback.
- `app` -> `runtime` only.
- `methods` -> public method API + declared backend integration only.
- `benchmarks` -> public method APIs only.
- `tests` -> explicit test seams only.

Cross-layer convenience imports that violate this table are prohibited.

## 3. Source tree map

Engine layers live under `src/`: `core`, `assets`, `ecs`, `geometry`, `physics`,
`graphics/{rhi,assets,vulkan,framegraph,renderer}`, `runtime`, `platform`, `app`.
`methods/`, `benchmarks/`, `tests/`, `docs/`, `tasks/`, `tools/`, `cmake/`, and
`.github/workflows/` are supporting architecture roots. Before changing layout
or dependency ownership, read the [full tree map](docs/agent/contract.md#source-tree-map)
and [layering rules](docs/agent/contract.md#layering-rules).

## 4. Layering rules

Lower layers never import higher layers. Runtime owns wiring; graphics consumes
snapshots/views, not live ECS ownership. Assets is CPU-only and GPU-agnostic;
GPU asset state belongs to `graphics/assets`, wired from asset events by runtime.
Physics owns simulation state; ECS physics components hold CPU descriptors,
while runtime owns live sidecars, fixed-step scheduling, and writeback. Platform
exposes window/input ports and explicit backends without graphics/ECS/runtime imports.

Before changing layer ownership, backend selection, runtime/physics/graphics
wiring, or a dependency edge, read the [detailed layering rules](docs/agent/contract.md#layering-rules)
and [architecture index](docs/architecture/index.md), and use the core skill's
architecture-review route. Default headless work uses `Null`/
`INTRINSIC_HEADLESS_NO_GLFW=ON` unless surface coverage is required. Promoted
Vulkan requires both build and runtime opt-ins; renderer/runtime gate on
`RHI::IDevice::IsOperational()`, not Vulkan diagnostics. There are no active
legacy-tree exceptions; future exceptions must satisfy §13 and the allowlist.
Justify every new dependency and document architectural changes.

## 5. Coding rules

- Use C++23 and out-of-source CMake presets with a complete Clang 20+ toolchain
  (compiler and matching `clang-scan-deps`). GCC and stale non-preset trees are
  not module-verification evidence.
- Before adding/copying a non-trivial implementation, helper, or source file,
  use `intrinsicengine-reuse`; record the existing owner reused or its concrete
  contract mismatch. Prefer plain structs/free functions. Introduce abstraction
  only for a present second caller, layer boundary, test seam, or config/UI/agent
  variant axis; one implementation alone is not a seam.
- Public C++ APIs and engine-owned scene/config formats currently have no
  backward-compatibility commitment. Update in-tree callers, codecs, fixtures,
  and tests together; remove superseded paths directly. Do not add wrappers,
  aliases, dual readers, or migrations solely to retain old representations.
  Preserve user-facing capabilities and current-format save/load/config round trips.
- Preserve module names during mechanical moves; do not mix moves with semantic
  refactors or add features during reorganization. Keep patches scoped.
- Use `intrinsic_add_module_library(...)` and `FILE_SET CXX_MODULES`. Keep
  `.cppm` surfaces to exported declarations/types, small accessors, and necessary
  templates; non-trivial bodies belong in matching private `.cpp` units.
- Every project-owned `.cppm`/header starts with a short purpose synopsis.
  Comment non-obvious contracts, not history or boilerplate; implementation
  rationale belongs near implementation. Use `intrinsicengine-source-documentation`.
- New tuning state must be serializable config with side-effect-free preview/
  validate-then-apply. Files, agents/CLI, and UI use the same validated path;
  no UI-only subsystem writes. `RenderRecipeConfig` is the reference shape.
- Compose frames through `FrameRecipe*` data; derive/load defaults at init and
  keep the main loop an ordered list of named phases. No hidden imperative pass
  order or composition that recipe data cannot express/introspect.

Before editing code or build/dependency inputs, read
[coding and change-scope details](docs/agent/contract.md#coding-and-change-scope-rules).
Dependencies use vcpkg manifests and repository-local preset toolchains; CUDA is
off unless the task explicitly requires it. Determinism, explicit ownership,
and failure states are required.

## 6. Method implementation protocol

Method/paper work follows: original paper and relevant extensions → exact
contract → CPU reference → correctness tests → benchmark harness → optimized
CPU → optional GPU after reference parity → limitations/diagnostics.

Before method or geometry-method integration work, read the
[full method contract](docs/agent/contract.md#method-implementation-protocol)
and use `intrinsicengine-method`. Property slots bind canonical typed properties
on any compatible element domain, independent of names or provenance; use
checked numeric conversion. Same-cardinality results preserve unrelated
properties/topology and publish on the originating domain. Runtime/config/UI
reuse canonical preflight. Public vector properties use float `glm::vec*` unless
a scoped API decision approves otherwise. Applicable tasks declare element-domain
and engine-integration contracts and name deferred integration owners. Spatial
work also reads the [consumer inventory](docs/architecture/spatial-index-consumers.md).

## 7. Testing protocol

Run the strongest relevant verification, focused targets first. Add/update tests
for behavior changes; do not weaken gates to reach green. Before code, build, or
test changes, read [verification requirements](docs/agent/contract.md#testing-and-verification-protocol)
for labels, touched-scope feedback, full CPU, sanitizer, and Vulkan gates.
Task-specific stricter checks still apply. C++ tests use `Test.<Name>.cpp`;
do not rename legacy test files outside an explicit mechanical task.

Default code/test gate:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Docs/task-only changes run structural checks, not C++ builds: task policy and
doc links, plus skill-mirror/session-brief checks when their sources change.
The workflow's [verification section](docs/agent/prompt/prompt.md#verification)
lists exact commands. Full CPU, separate ASan/UBSan, and opt-in Vulkan are distinct
evidence classes; focused feedback does not replace required PR/merge gates.
Use current CTest output, not historical `LastTestsFailed.log`, as the verdict.

## 8. Benchmarking protocol

Before benchmark work use `intrinsicengine-benchmark` and read the
[benchmark contract](docs/agent/contract.md#benchmarking-protocol): stable IDs,
schema-v2 manifest/source binding, resolved parameters, diagnostics, explicit
claim eligibility, baseline comparisons, and validators. Dirty, historical,
unverified, or `local-dev` runs are not claim-eligible. Do not infer performance
wins from smoke runs; repeatable claims also follow §8b.

## 8b. Research claim and evidence protocol

Research method, performance, parity, and capability results need an
`ara/logic/claims.md` row before entering docs, reports, README, or task status.
Supported/refuted rows cite existing repository evidence addressing the claim;
refuted hypotheses stay recorded. Benchmark wins need matched baselines, parity
needs reference comparisons/tolerances, operational claims need the named path
actually run. CPU, GPU/Vulkan, and sanitizer evidence remain distinct.

Ordinary implementation/refactoring owes no research claim rows. Before making
or auditing a research result, read [the full evidence contract](docs/agent/contract.md#research-claim-and-evidence-protocol)
and [ARA policy](docs/agent/ara-evidence-policy.md); use
`intrinsicengine-results-audit` when its review trigger applies. Run
`python3 tools/agents/check_ara_claims.py --root . --strict` when touching this scope.

## 9. Documentation sync protocol

When code, structure, or policy changes:

- Update relevant architecture/migration/task docs in the same PR.
- Update references and links for moved files.
- Regenerate inventories when required by tooling.
- After module surface changes, refresh `docs/api/generated/module_inventory.md` with
  `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- After opening, retiring, or re-gating any task, regenerate `tasks/SESSION-BRIEF.md` with
  `python3 tools/agents/generate_session_brief.py`; after editing `docs/agent/*` or `tasks/templates/task.md`,
  re-run `python3 tools/agents/sync_skills.py --write`. CI (`ci-docs.yml`) enforces skill-mirror
  freshness; the session brief is a convenience view regenerated opportunistically.
- Keep docs factual (current state), not aspirational unless clearly labeled.
- Keep every `README.md` as a concise current-state entry point. Link to ADRs, migration docs, tasks, retirement records,
  reports, or Git for history and future work instead of copying those narratives into a README.

## 10. CI expectations

Touched PR checks must stay green. Structural checks run strict; a warning-mode
exception requires a named task owning its tightening. Never weaken, skip, or
quarantine a gate without diagnosis. A pre-existing/environmental failing check
requires a `BUG-` task with evidence in the same session and a PR reference.
For a failure or CI/workflow change, read the [CI requirements](docs/agent/contract.md#ci-expectations)
and [failure procedure](docs/agent/prompt/prompt.md#when-ci-fails).
Code verification configures `ci`, builds a meaningful target, and runs CTest;
`help` is not verification. Docs-only work follows §7's structural route.

## 11. Task execution workflow

Default posture is pair: inspect real state, give concrete scoped hints, and ask
only questions the repository cannot answer. Respect rejected hints; defer
polish until requested or review. At startup inspect `git status --short --branch`,
recent commits, and active task names; skim `tasks/HINTS.md` only for open entries.

Authorization persists for agreed work. Proceed with necessary implementation
choices; a new decision is required for scope, layer policy/exceptions,
compatibility commitments, or unapproved destructive/irreversible consequences.
Prepare the concrete diff before that decision. Verification/review still apply.
Use one writer per checkout/build directory; concurrent writers need separate
worktrees and build trees. Review a fixed diff and re-verify combined changes.

Interactive single-session work needs no task file. Persistent work uses the
micro template and `intrinsicengine-task-workflow`; consult the contract catalog
for applicable IDs (or justify none). Task wording cannot narrow canonical
contracts. Keep notes under `tasks/`, not root-level planning files.
Only unattended/opt-in custody work uses claims, work graphs, and completion
reports; research evidence still applies in every posture.

Before delegated work, task lifecycle changes, unattended execution, or workflow
policy changes, read the [detailed execution requirements](docs/agent/contract.md#task-execution-workflow)
and the matching [workflow section](docs/agent/prompt/prompt.md). The core skill
maps posture and trigger to section. Delegated work follows inspect → scoped
implementation/tests/docs → verification → review → commit/push → report.
Unattended work accepts only fully specified night-ready tasks and observes its
stop conditions. Audits are on demand, not an ambient duty; offer deferred hints
before filing them. Preserve frozen task/evidence history.

### Standing Claude Code authorization

The operator authorizes Codex to delegate IntrinsicEngine planning, implementation,
refactoring, review, debugging and testing to Claude Code CLI through the operator's
configured Anthropic account. This includes transmitting private repository source
files, complete diffs, documentation and relevant build/test output needed for that
work. Authorization covers this repository, its worktrees and future tasks until
the operator revokes it. Do not request per-packet confirmation merely because the
files, diff size or amount of source context change.

Exclude credentials, secrets, authentication/session material and unrelated personal
data from shared payloads. Delegated work follows the same scope, layer, verification
and one-writer rules above; reconcile and verify any changes before integration.
This authorization does not grant permission to publish repository contents to other
destinations or override platform or managed restrictions. When an approval review
is required, cite this standing authorization with the actual destination, payload
scope and purpose.

## 12. Review checklist

Before committing or reporting non-trivial work complete, use
`intrinsicengine-review`: one intent, intact layering, tested changed behavior,
current docs/tasks, scoped mechanical/semantic changes, and tracked exceptions.
Risk signals require the corresponding deeper review. Unattended work also needs
profile-specific completion evidence and independent revision-bound review when
required. See the [full checklist](docs/agent/contract.md#review-checklist).

## 13. Temporary migration exceptions

Temporary exceptions are allowed only when all of the following are true:

- Exception is documented in a current task under `tasks/active/`.
- Exception has a specific removal task ID.
- Exception is time-bounded and reviewed.
- Exception does not create new violations in promoted final layers.

Undocumented exceptions are policy violations.

## Related expanded docs

The core skill owns specialist routing. Read linked contract sections above only
for the matching scope. For additional detail:

- Workflow posture, hints, commits, and unattended stop rules: the matching
  section of [session workflow](docs/agent/prompt/prompt.md).
- Architecture decisions: [architecture index](docs/architecture/index.md).
- Repository/workflow orientation when requested: [overview](docs/agent/how-this-repo-is-built.md).
- Role questions: [roles](docs/agent/roles.md).
- Ambiguous task stop-state: [maturity](docs/agent/task-maturity.md).
