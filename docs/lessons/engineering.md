# Engineering safeguards

These lessons summarize completed work; no measurements were rerun for the
retention cleanup. Exact prior artifacts remain in Git at
`36a7b5062d9be3239d667efd9e41c4d4028ba249`.

## Compile cost follows work, not file size

Before moving ownership, identify repeated parsing/instantiation that the move
can actually remove. Measure representative consumers and the receiving owner;
fewer imports, a smaller BMI, or many consumers do not establish a speedup.
Preserve complete-type and lifetime constraints. The recorded tradeoffs remain
in [RUNTIME-266](../../tasks/done/RUNTIME-266-editor-snapshot-consumer-locality.md),
[RUNTIME-268](../../tasks/done/RUNTIME-268-workspace-snapshot-bmi-serialization.md)
and [GRAPHICS-145](../../tasks/done/GRAPHICS-145-renderer-implementation-frontend-cost.md).
The [compile-hotspot checks](../../tests/regression/tooling/Test.CompileHotspots.py)
exercise the attribution tooling.

Two closed feasibility investigations shipped no implementation:

- EnTT `extern template` declarations for private deduced-return members or
  storage classes still instantiated the allocation chain. Class declarations
  removed only part of it, and exposing registry headers adds dependency cost.
  A future attempt needs a different demonstrated boundary; this rejects those
  candidates, not every EnTT optimization. The existing component owner is
  `src/ecs/Components/ECS.Component.GeometrySources.cpp`.
- Moving mock lifetime bodies out of line added code without a useful consumer
  diagnostic reduction and increased receiving-owner work. Source was restored.
  Single compiler samples were feasibility observations, not target-build
  measurements; the target speedup criteria were never tested. Establish
  removable instantiation cost before repeating this extraction.

Original scoped reports: [EnTT feasibility](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/ara/evidence/diagnostics/entt_storage_rejected/README.md)
and [mock lifetime pilot](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/ara/evidence/diagnostics/mock_lifetime_rejected/README.md).

## Test asynchronous states deliberately

When a test observes a running job and then its completion, hold the job at
that observation boundary through the existing submit/work seam. Repeated
passing runs cannot exclude a race: BUG-219's test sometimes observed completion
twice while the production cache was correct. Avoid timing sleeps and dummy
jobs; order gate/lock lifetime so teardown can release the worker.
The [BUG-219 record](../../tasks/done/BUG-219-uv-panel-cache-snapshot-race.md)
and [runtime contract test](../../tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp)
retain the actual safeguard.

## State the GPU path that actually ran

GPU neighborhood queries with CPU reductions are not full GPU arithmetic.
Bounded readback tests do not establish arbitrary-input parity, speedup,
universal cancellation safety or leak freedom. Keep requested/actual/fallback
reporting specific to the selected path. See
[RUNTIME-227](../../tasks/done/RUNTIME-227-compact-density-weight-spatial-backends.md),
[RUNTIME-229](../../tasks/done/RUNTIME-229-lop-framed-lbvh-projection.md), and the
[LBVH smoke](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp).

Immediate fenced readback also does not eliminate once-per-frame pump latency
for small iterative jobs. [GRAPHICS-150](../../tasks/done/GRAPHICS-150-low-latency-compute-readback.md)
records that unresolved distinction and its follow-up owner.

For unexplained module-layout or virtual-dispatch failures, reuse the existing
[stale-build triage procedure](../../tools/agents/skills/intrinsicengine-stale-build-triage/SKILL.md)
before changing source; do not duplicate its cache-disabled reproduction rules.

## A fail-closed message must not name a single cause

Five weekly `ci-source-coverage` runs (2026-09-07 to 2026-10-19) failed with an
error that blamed non-atomic profile updates, although atomic updates were
already enabled. The real cause was a
Clang counter-map gap: a `?:` with lvalue operands, one a named constant,
written directly inside a braced initializer borrows the function-entry
counter. Before blaming threads, check whether the bad region's true count
equals its function's count. The collector error now names the location and
both causes, and the [collector notes](../../tools/ci/README.md) give the
coding rule. [BUG-242](../../tasks/done/BUG-242-cpu-source-coverage-counter-invalid.md)
records the diagnosis.

## Attribute unknown-module leak frames before blaming a driver

LeakSanitizer prints `<unknown module>` for code in a library that unloaded
before exit; here the Vulkan loader had unloaded the ICD libraries. BUG-221's two
256-byte "NVIDIA" shutdown leaks came from the unselected lavapipe and RADV
ICDs: a dynamic-loader map trace placed the PCs inside `libvulkan_lvp.so` and
`libvulkan_radeon.so`, and a 13-line instance/enumeration probe reproduced the
512 bytes without engine code. Map unknown PCs to load ranges, reproduce with an
engine-free probe, and select exactly one ICD before reading a leak report; the
[shutdown runner](../../tests/regression/graphics/Run.VulkanShutdownLsanContract.cmake)
now rejects an ambiguous selection.
Never add loader, ICD or unknown-module suppressions. Qualify on the driver's
native display: the same build and NVIDIA ICD still failed on Xephyr while the
native X11 run passed (BUG-229). A nested-display result is a separate WSI-path
finding, not evidence for or against an engine leak.

## Check GPU synchronization with the validator that sees it

Core validation stayed silent while the tested default-recipe scenes had depth
hazards: passes that only read depth still stored it, transitions into the
depth-write state hid the depth from the next load, and the single-submit path
did not chain the first backbuffer transition to the acquire. Khronos
synchronization validation (layer 1.3.296,
`VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`)
reported 36 and 42 hazards there. The validation smokes now enable it through
`OperationalCounterStability::EnableSynchronizationValidation` and expect no
error. It does not see everything: it reported nothing for barrier-less
same-state attachment writes between passes or for depth writes into a
read-only depth attachment, so those rules rest on CPU barrier and recipe
tests and the Vulkan requirements. The graph boundary and cross-queue
synchronization were outside that check.
For draw-order assertions on the GPU, put the primitives in one draw: the
cull appends draws of different entities in atomic order, which creation
order does not decide. A mutation that breaks a draw's vertex offset can lose
the device instead of failing a click. See
[BUG-255](../../tasks/done/BUG-255-render-graph-depth-synchronization-hazards.md)
and [BUG-254](../../tasks/done/BUG-254-gpu-pick-misses-points-and-lines.md).

## A workflow edit needs its pinning test, and a push needs its CI result

`Test.CheckTaskStateLinks.py` pins the exact command list of the ci-docs step "Validate structural CI policy
regressions". ARCH-022 added a test to that step without updating the pin, and the check before pushing ran only a
hand-picked subset of ci-docs. ci-docs was then red on `main` for seven commits (`b73d90b46` to `ef98e7370`) until
`b6963920e`. Safeguard: before pushing a change under `.github/workflows/`, search the tests for the edited step name and
run every `python3` command of that workflow on a clean worktree of the commit; after each push, read that commit's CI
result before pushing the next. A replay that collects the commands from the workflow file must also take single-line
`run: python3 ...` steps; ARCH-022 missed 14 of them once and broke `main` again (`01095d834`). `ci-linux-clang`
runs repository checks before its build (`check_layering.py --root src --strict`, `check_compiler_hazards.py`);
replay those too for a change under `src/`. Replay whole `run:` blocks with `bash -e`, not extracted `python3`
lines: a generator followed by `git diff --exit-code` (the module inventory) only fails as a block. `ci-linux-clang`
also captures the CPU test selection from the CTest inventory (`tools/ci/cpu_test_selection.py capture`): a new
CTest test that is no GoogleTest binary must run an allowlisted script (`SCRIPT_TESTS`). ARCH-022 registered
`PluginBuildId.Script` without it and broke `main` (`bfb76bb39`); run the capture on a built tree when the change adds
a CTest test.

## Do not wrap a push in `git stash push`/`pop`

`git push` sends commits, never the working tree, so nothing has to be stashed around it. ARCH-022 ran
`git stash push -- tasks/active/` before a push when that path had no changes: the push saved nothing, and the
following `git stash pop` applied an older, unrelated stash (`BUG-137 slice C WIP`) onto the tree and stopped on a
conflict. No work was lost (a conflicted pop keeps the entry), but the tree had to be restored by hand. Safeguard:
push without stashing; when a stash is really needed, check `git diff --quiet -- <path>` first and pop the exact
entry it created (`git stash pop stash@{n}` after reading `git stash list`).

## A build tree a driver uses holds only what the driver measures

The plugin drivers (`plugins/fixture`) build in the tree they run against and check that a plugin build compiles only
plugin edges. ARCH-022 slice 3 broke that twice: a mutation run rebuilt only the test aggregate and left the Sandbox
stale, so the next technical R4 run rebuilt it and failed `plugin_edges_only`; later a comment edit in a module
interface during a driver run would have rebuilt the host inside the S2 session (restoring the file's content and
mtime avoided it, since Ninja compares mtimes, not content). Safeguard: run `cmake --build <tree>` (all targets) after
any mutation or source edit and before a driver run, and edit nothing under `src/` while a driver runs.

## A full rebuild must fit the machine's memory

On 2026-11-02 the workstation hung twice and was reset. The second time, `cmake --preset dev` was run on the existing
static tree `build/dev` only to pick up lld; the preset sets `BUILD_SHARED_LIBS` and position-independent code, so all
2804 objects were invalidated and Ninja's default of nproc+2 (34) compiles ran at once. A heavy C++23 module TU under
ASan peaks at about 3.4 GB, which outgrew 62 GB of RAM with 1 GB of swap. Safeguard: compiles share the Ninja pool
`intrinsic_compile`, one per 4 GiB of RAM (`cmake/CompilePool.cmake`, `Test.CompilePool.py`); before configuring an
existing tree with a preset, compare its `BUILD_SHARED_LIBS` with the preset's, and keep a static tree in its own
directory.

## A frozen run command is tested as written

ARCH-022 S3c-3's first start ended at once, before anything ran: the frozen command
`tasks/evidence/ARCH-022/kmeans_stall_run.sh <out>` exited 127 ("No such file or directory", cause not traced), and the
run script had no execute bit (which alone gives exit 126, "Permission denied"); after `chmod +x` the identical command
ran. The technical tests had started the harness binary directly, and the packet's manifest hashes content, not file
modes, so neither would have caught a non-executable script. Safeguard: the start gate checks `test -x` on a run
script it calls without an interpreter (or the command names the interpreter), and at least one technical test goes
through the run script itself.
