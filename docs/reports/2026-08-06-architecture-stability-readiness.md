# Architecture stability and right-sizing readiness — 2026-08-06

## Verdict

**Ready at the audited commit.** Clean `main` commit
`51e7faddad943ab7727e407d008e474ec076566d` satisfies the REVIEW-003
architecture, clean-workshop, drift, agent-output, right-sizing, task-map, and
default CPU gates. No readiness-blocking finding remains and no remediation
task was opened by this fresh run.

This is a commit-scoped baseline, not a claim that the architecture can never
regress. It does not promote deferred research/rendering ideas automatically,
nor does it claim fresh GPU/Vulkan or sanitizer evidence.

## Audited state

| Field | Value |
| --- | --- |
| Commit | `51e7faddad943ab7727e407d008e474ec076566d` |
| Branch | `main` |
| Captured | 2026-08-06 20:50 CEST |
| Worktree | clean (`git status --short` empty) |
| Host | Linux `alex-home`, kernel 6.14.0-37-generic, x86_64 |
| Build tools | CMake 3.28.3; Clang, clang++, and clang-scan-deps 23.0.0 |
| Build identity | fresh `ci` preset, Null/headless default capability lane |
| Evidence | [`tasks/evidence/REVIEW-003/`](../../tasks/evidence/REVIEW-003/) |

The rejected REVIEW-003 baseline at `fcbb2fee` and all partial evidence from
that run were discarded. Every command and manual judgment below was rerun or
re-read against the fresh commit named above.

## Dependency and target-state disposition

All 47 static front-matter dependencies resolve to `tasks/done/` or
`tasks/archive/`; the exact ID-to-path receipt is
[`dependency-retirement.stdout.log`](../../tasks/evidence/REVIEW-003/commands/dependency-retirement.stdout.log).
This includes every blocker opened by prior re-gates and the rejected audit.

The retired [`ARCH-014`](../../tasks/done/ARCH-014-kernel-convergence-tracking.md)
scorecard remains all-green. The checked Engine boundary is exactly
`12/0/0/5`: twelve declaration-required plain imports, zero domain imports,
zero re-exports, and five exact kernel getters, with no temporary debt.

Historical right-sizing-origin blockers are closed as follows:

- The 2026-07-24 runtime surface audit's parallel-path and ownership leaves
  (`RUNTIME-191..204` plus `PHYSICS-004`) are retired.
- The 2026-08-01 coherence re-gate's speculative/plumbing leaves
  (`RUNTIME-139`, `RUNTIME-203`, and `RUNTIME-205`) are retired.
- The focused deletion-test gate `RUNTIME-216` is retired.
- The rejected clean-baseline findings `GRAPHICS-129..134`, `RUNTIME-217`,
  `HARDEN-088`, and `RORG-031E` are retired.
- No currently open task originates from an unresolved REVIEW-003
  right-sizing finding.

## Architecture-review checklist

Every row in
[`architecture-review-checklist.md`](../agent/architecture-review-checklist.md)
is recorded below. “Not applicable” means the report-only task introduced no
change that can create that failure mode; it is not used to excuse a current
whole-tree finding.

| ID | Checklist row | Outcome | Evidence / rationale |
| --- | --- | --- | --- |
| L1 | Owning layer/subsystem is explicit | pass | The audited source map follows the repository layer roots; runtime owns composition and the flagged surfaces have named owners. |
| L2 | New dependency edges are justified | not-applicable | REVIEW-003 adds reports/task state only and creates no code or CMake edge. |
| L3 | No lower layer imports higher layers | pass | Strict layering scanned 7,163 import/include references and found zero violation. |
| L4 | Runtime remains composition root | pass | Engine publishes exact lower-layer ports/services; app composes runtime modules; lower layers do not acquire runtime ownership. |
| L5 | Strict checker covers module and CMake edges | pass | The run covered 746 files plus 85 CMake link references; the allowlist has zero entries. |
| B1 | Parallel algorithms declare backend axis/deferment | not-applicable | No algorithm or backend is added or changed by this audit. Existing method/backend contracts remain task-owned. |
| B2 | Config/UI uses the shared round-trippable lane | not-applicable | No tuning state or UI mutation route changes. Current recipe/config samples use preview/validate/apply paths. |
| O1 | Ownership model is explicit | pass | Right-sizing reads confirmed value/DTO, unique-owner, handle, borrowed-view, and scoped-connection lifetimes for every flagged surface. |
| O2 | Cross-system references avoid hidden coupling | pass | Graphics consumes snapshots/views, runtime owns ECS/lower-layer handoff, and retained borrows have explicit teardown/rebind owners. |
| O3 | Temporary shims are tracked | pass | No temporary migration shim or allowlist exception remains; technical temporary storage is not compatibility debt. |
| C1 | Threading model is explicit | pass | `JobService`, transfer/RHI queues, renderer recording, callback lifetime, and main-thread apply paths state their execution boundary and are contract-tested. |
| C2 | Shared state synchronization is clear | pass | Lock-free queue, job cancellation/finalization, scoped connections, registry generations, and RHI queue ownership are load-bearing reviewed seams. |
| C3 | No unjustified hot-path blocking added | not-applicable | REVIEW-003 changes no runtime path. The audit found no new synchronous wrapper or blocking hop. |
| E1 | Failure states propagate deterministically | pass | Sampled config, asset, job, render recipe, upload, registry, and backend surfaces use explicit result/diagnostic/fail-closed states. |
| E2 | New failures have actionable diagnostics | not-applicable | No behavior or failure mode is introduced by this report-only task. |
| E3 | Fallback behavior is documented | pass | Null/headless and promoted-Vulkan selection, operational checks, upload fallback, and recipe rejection are documented and tested in their supported lanes. |
| T1 | Test categories/labels are correct | pass | Strict test-layout validation passes; no label or test registration changes. |
| T2 | Verification subset is strong enough | pass | A fresh configure, full `IntrinsicTests` build, and complete default CPU selector passed on the exact baseline. |
| T3 | Behavior changes add tests | not-applicable | No behavior changes. Existing behavioral claims sampled by the audit resolve to tests. |
| P1 | Performance-sensitive change assessed | not-applicable | No performance-sensitive implementation change. |
| P2 | No unsupported performance claim | pass | This report makes no throughput/latency improvement claim. |
| P3 | Smoke vs deep benchmark expectations explicit | not-applicable | No benchmark or method result changes; capability limits are stated below. |
| D1 | Architecture/path docs synchronized | pass | Strict docs sync, link checking, and exact module-inventory regeneration pass. |
| D2 | Task tracker updated for blockers/exceptions | pass | All 47 blockers are retired; the fresh run found no new exception or blocker. |
| D3 | Mechanical and semantic work separated | pass | This task is reports/task-state only; remediation landed in independent prior tasks. |
| D4 | Follow-up cleanup recorded | pass | No fresh finding requires a follow-up; every earlier finding names a retired owner. |
| D5 | Maturity stated accurately | pass | REVIEW-003 closes only a commit-scoped readiness gate; it does not relabel backend or feature maturity. |
| CI1 | PR template sections complete | not-applicable | The user authorized direct completion on `main`; no PR is created. This report records scope, layering, tests, docs, performance, review, and shim disposition explicitly. |
| CI2 | Workflow impacts reviewed | not-applicable | No workflow file or trigger changes. |
| CI3 | Strict validators remain green | pass | Clean-workshop, tasks, docs, layering, test-layout, root-hygiene, and inventory gates all pass. |

## Theme F open-task inventory

Seven Theme F tasks were open at the audited commit. Only REVIEW-003 was a
readiness block, and it was the gate being executed.

| Task | Classification | Rationale |
| --- | --- | --- |
| `REVIEW-003` | blocking, now satisfied | This report and its evidence are the architecture-readiness gate itself. |
| [`ASSETIO-010`](../../tasks/backlog/assets/ASSETIO-010-async-model-companion-preflight.md) | nonblocking | Future operational async-import companion preflight; explicitly forbids new service/registry/queue frameworks and is not current architecture debt. |
| [`ASSETIO-011`](../../tasks/backlog/assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md) | nonblocking | Future real-widget/import matrix, dependent on ASSETIO-010 and bug leaves; it strengthens coverage rather than repairing a dead current seam. |
| [`BUG-134`](../../tasks/backlog/bugs/BUG-134-imgui-adapter-panel-draw-list-intermittent.md) | nonblocking at this baseline | The task explicitly becomes a REVIEW-003 dependency only on recurrence. The fresh complete CPU gate passed without recurrence. |
| [`PLATFORM-004`](../../tasks/backlog/platform/PLATFORM-004-alternative-platform-backend-onboarding.md) | nonblocking | Planning-only alternative-backend seed; no speculative production abstraction or current correctness debt. |
| [`LEGACY-043`](../../tasks/backlog/rendering/LEGACY-043-retire-stale-multiset-shaders.md) | nonblocking | Deferred deletion of inactive shader sources after `GRAPHICS-105`; the promoted pipeline does not load them, so they are not a live public seam or current architecture exception. |
| [`UI-037`](../../tasks/backlog/ui/UI-037-linear-domain-action-readiness-tooltips.md) | nonblocking | Future operational readiness/UX work that reuses current validated paths and explicitly forbids a global facade/service. |

## Right-sizing inventory

### Method

The inventory covers every exported `I*` declaration and every public
`*Service`, `*Bridge`, `*Registry`, `*Queue`, `*Binding`, and `*Submission`
name in the audited tree. Counts are file counts, not raw textual hit counts:

- for interfaces: production implementations / non-definition production
  reference files / test reference files;
- for other surfaces: one defining surface / non-definition production
  reference files / test reference files.

The “deletion test” is a counterfactual ownership/contract test, not an
uncommitted source deletion: REVIEW-003 is forbidden from absorbing fixes. A
surface stays only if it has real variants, isolates a volatile boundary,
carries correctness/lifetime machinery, or is required for deterministic
layering/failure state. Plain DTOs are not treated as frameworks merely because
their names match a probe suffix. An independent read-only agent repeated the
code/call-site audit and found no blocker.

### Interfaces

| Surface | Counts | Keep-list and deletion-test result | Verdict |
| --- | --- | --- | --- |
| `IIOBackend` | `1 / 6 / 2` | Volatile file-IO boundary; FileIO plus Memory/Blocking test implementations make deterministic IO and cancellation tests possible. Removing it destroys the hermetic fake seam. | retain |
| `IRenderer` | `1 / 20 / 28` | The sole production implementation is private and roughly 10k lines. The interface/factory is the graphics↔runtime compile firewall; deleting it would expose that implementation or replace it with an equally large forwarding PImpl. It is heavily consumed and tested. | retain |
| `IBindlessHeap` | `3 / 8 / 2` | Real Null, Vulkan, and fallback implementations at a volatile GPU descriptor boundary. | retain |
| `ICommandContext` | `4 / 91 / 30` | Real Null/Vulkan plus runtime no-op command contexts; it is the backend command-recording boundary used throughout graphics/runtime. | retain |
| `IDevice` | `2 / 80 / 30` | Canonical Null/Vulkan backend pair; required for the headless CPU gate and backend-neutral runtime. | retain |
| `IProfiler` | `2 / 6 / 4` | Real Null/Vulkan implementations with native/fallback timing contracts; prior dead/misleading behavior was remediated by `GRAPHICS-127`. | retain |
| `ITransferQueue` | `3 / 17 / 17` | Real Null, fallback, and Vulkan transfer implementations plus test doubles; owns async completion/readback semantics. | retain |
| `IWindow` | `2 / 14 / 17` | Canonical Null/GLFW platform pair; required for explicit headless selection. | retain |
| `ICameraController` | `4 / 6 / 10` | Four live strategies (orbit, fly, free-look, top-down) and selection/transition consumers. | retain |
| `IRuntimeModule` | `12 / 14 / 9` | Twelve production lifecycle owners use boot/resolve/init/shutdown composition; no one-entry framework remains. | retain |

### Other flagged surfaces

| Surface | Counts | Keep-list and deletion-test result | Verdict |
| --- | --- | --- | --- |
| `AssetRegistry` | `1 / 6 / 2` | Owns generational asset identity and deterministic metadata lookup. Inlining would duplicate authority across asset consumers. | retain |
| `AssetService` | `1 / 12 / 11` | Owns transactional load/reload/unwind, payload state, and events across multiple runtime consumers. It is not a forwarding facade. | retain |
| `CallbackRegistry` | `1 / 2 / 1` | Generational, thread-safe callback lifetime reused by live asset/event paths; deletion loses disconnect correctness. | retain |
| `EngineConfigSectionRegistry` | `1 / 3 / 5` | Typed multi-section extension used by boot, control, app config, and tests while keeping core config independent of runtime types. | retain |
| `LockFreeQueue` | `1 / 1 / 1` | Actual bounded synchronization primitive used by Core.Tasks, not delegation machinery. | retain |
| `PropertyRegistry` | `1 / 1 / 0` | Owning typed/revisioned storage beneath `PropertySet`; the zero direct-name test count is a probe artifact because geometry/runtime tests exercise it through property handles/sets. Removing it erases the storage authority. | retain |
| `FrameRecipePassContributionRegistry` | `1 / 1 / 1` | Plain vector-backed recipe data carrying four live overlay/finalization contributions under P5; no registration framework or dynamic plugin lifecycle. | retain |
| `GpuBufferUploadSubmission` | `1 / 0 / 1` | Plain explicit result for staged/fallback/rejected upload. Production mostly consumes the `SubmitBufferUpload` function via `auto`/discard, so the type-name count is zero; exact result behavior is contract-tested. | retain |
| `RenderSubsystemRegistry` | `1 / 2 / 2` | Owns partial initialize, operational rebuild, diagnostics, and reverse shutdown for 17 concrete stages. Deletion would return lifetime ordering to renderer member sprawl. | retain |
| `PlacedResourceBinding` | `1 / 1 / 1` | Plain RHI DTO shared by Null/Vulkan placed-resource descriptors; required for explicit alias/placement state. | retain |
| `VulkanTransferQueue` | `1 / 2 / 0` | Concrete Vulkan implementation of the justified `ITransferQueue`, not another abstraction layer. | retain |
| `CameraControllerRegistry` | `1 / 13 / 14` | Multiple live controllers and consumers; owns selection, transitions, and controller lookup. | retain |
| `EditorWindowRegistry` | `1 / 6 / 11` | Multiple registered editor windows/panels with menu/open-state and callback lifetime. | retain |
| `VertexAttributeBinding` | `1 / 15 / 8` | Plain declarative DTO for semantic property→GPU attribute mapping with broad consumers. | retain |
| `VertexChannelSourceBinding` | `1 / 5 / 1` | Plain declarative channel-source DTO, not a wrapper/factory. | retain |
| `RuntimeInputActionBinding` | `1 / 4 / 3` | Plain action-binding DTO shared by app/runtime input composition. | retain |
| `RuntimeInputActionRegistry` | `1 / 3 / 4` | Multiple live app/runtime actions; owns deterministic dispatch and input-capture gating. | retain |
| `JobService` | `1 / 38 / 28` | Load-bearing cancellation, stale-result validation, finalize-on-main-thread, GPU participant, and shutdown machinery. | retain |
| `ServiceRegistry` | `1 / 31 / 11` | Typed multi-provider/consumer resolution with phase-aware `Require` failure. Twelve modules use it; it is not a one-entry service locator. | retain |
| `WorldRegistry` | `1 / 35 / 12` | Generational multi-world lifetime and deferred-operation authority. Removing it would duplicate active-world validation. | retain |
| `ClusteringService` | `1 / 6 / 4` | Narrow typed multi-consumer capability over canonical clustering CPU/GPU execution; prior parallel paths were removed by `RUNTIME-196`. | retain |
| `PointCloudConsolidationService` | `1 / 3 / 3` | Narrow typed multi-consumer capability with explicit async/GPU state; it hides generic bus details rather than forwarding to a duplicate service. | retain |
| `TextureBakeService` | `1 / 11 / 9` | Stateful operation/lifetime owner for bake submission, completion, residency, and diagnostics across app/runtime consumers. | retain |
| `RenderArtifactRegistry` | `1 / 2 / 5` | Validated publication/apply/undo/audit state machine; removal loses explicit artifact lifecycle and failure diagnostics. | retain |
| `StableEntityLookupSceneBinding` | `1 / 1 / 0` | Internal helper owning three EnTT scoped connections and exact disconnect/rebuild order inside `SceneInteractionModule`. Direct-name tests are zero, but scene interaction tests exercise construct/update/destroy, world replacement, and no-resurrection behavior. | retain |

No public `*Bridge` declaration remains. The added role-named adapter sample is
also clean: `CurrentRendererContractAdapter` is data-only with three production
consumers and broad tests; `ImGuiAdapter` isolates the volatile Dear ImGui /
platform / graphics handoff and is owned directly by `EditorUiModule`.

## Standard audit results

- [Clean-workshop review](../reviews/2026-08-06-clean-workshop-review.md): all
  eight rows pass.
- [Repo-state drift audit](2026-08-06-drift-audit.md): all nine rows pass.
- [Agent-output audit](2026-08-06-agent-output-audit.md): all nine rows pass.

## Verification

| Gate | Result |
| --- | --- |
| `cmake --preset ci --fresh` | pass |
| `cmake --build --preset ci --target IntrinsicTests` | pass |
| Default CPU selector | pass: 4,102/4,102; zero failed; expected headless `GlfwLifecycleLsan` skip |
| `run_clean_workshop_review.sh . --strict` | pass |
| Strict layering / allowlist quality | pass: 746 files, 7,163 imports/includes, 85 CMake links, zero violations/entries |
| Task policy / schema / state links | pass |
| Docs sync / links | pass; 3,179 relative links checked |
| Test layout / root hygiene | pass |
| Fresh module inventory vs committed inventory | pass; exact diff |
| Audit cadence / generated session brief | pass; dated reports are current and the post-retirement brief is regenerated |

## Evidence limits

- The default CPU lane is fresh. Promoted GPU/Vulkan execution, ASan, and
  UBSan were not rerun for this report-only task and are not claimed here.
- `VulkanTransferQueue` and native `IProfiler` operational behavior therefore
  rests on their capability-specific task evidence, not this fresh CPU run.
- `IRenderer` has no second production implementation; its retain verdict
  rests on the volatile compile-firewall/deletion test and broad consumption.
- `PropertyRegistry` and `StableEntityLookupSceneBinding` are tested through
  their owning public operations rather than tests naming those internal
  types directly.

## Final disposition

The audited commit has no unresolved premature abstraction, one-consumer
framework, pure-forwarding facade, fragmented feature framework, speculative
public generalization, dead public seam, untracked compatibility shim,
temporary migration exception, stale architecture claim, or unowned source
marker. Deferred idea tasks may now treat REVIEW-003 as satisfied, subject to
their own dependencies and normal per-task review.
