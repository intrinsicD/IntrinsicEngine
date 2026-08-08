---
id: BUG-143
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug143"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-08T09:58:41Z"
contract_schema: 1
contracts:
  - geometry.property-coherence
---
# BUG-143 — Corner-UV `gpu;vulkan` smoke exceeds the 30 s cohort timeout

## Status

- Completed and retired on 2026-08-08.

### Diagnosis — the variance is present pacing, and the host owns it

The 13 s ↔ 34 s spread is not noise in the test's work. It is
`vkQueuePresentKHR` blocking for about a second per frame whenever the X11
display is not being scanned out.

Measured on this host (RTX 3050, driver 590.48.01, `ci-vulkan`) with the
engine's own `--frame-pacing-report` capture on `ExtrinsicSandbox`, 32 frames,
same binary, same session, only the display power state changed:

| Display state | Frame total | `present_micros` |
| --- | --- | --- |
| `Monitor is Off` (DPMS) | ~1000 ms, every frame | ~900 ms |
| `Monitor is On` | ~103 ms | ~0 ms |

The remaining ~100 ms in the throttled case is the swapchain acquire inside
`render_prepare`, so a throttled frame is pinned at almost exactly 1 Hz. The
restored 96-frame smoke therefore costs ~1.6 s of presents with the screen
awake and ~96 s with it asleep; standalone runs of the unfixed test on this
host measured **97.78 s ± 0.02 s across nine consecutive runs** with the
monitor off, and the ctest cohort killed it at its 30 s budget. That is the
same mechanism behind the original host's 12.7 s / 33.9 s pair — hypothesis 1
in the task's suspect list ("swapchain/present pacing in `ExitAfterFramesApp`
under a non-VSync config"), confirmed.

Ruled out along the way:

- **Deferred enrichment wake latency** — the run log timestamps put "Direct
  mesh enrichment applied" 0.4 s after device init, with the remaining ~95 s
  spent entirely inside the frame loop with no output.
- **First-use pipeline/shader compilation attributed to whichever test runs
  first** — the cost is per frame, not per process, and it is identical on
  frames 1 and 31.
- **ctest environment (sanitizer options, working directory)** — running the
  binary directly with ctest's exact `ASAN_OPTIONS`/`LSAN_OPTIONS` and
  `WORKING_DIRECTORY` reproduced 97.7 s in all three combinations.
- **`__GL_SYNC_TO_VBLANK=0` / `vblank_mode=0`** — no effect; these are GL
  knobs and the throttle is in the Vulkan WSI path.
- **CPU work** — the process sits at 12% CPU with the main thread parked in
  `hrtimer_nanosleep` for the whole loop.

### Fix — stop on the condition, bounded by the wall clock

A frame count was the wrong unit: the smoke spun 96 frames to wait for the
deferred atlas-UV enrichment job, and frame duration is owned by the display
stack. `ExitWhenReadyApp` replaces `ExitAfterFramesApp` for this smoke. It
takes a readiness predicate (corner `h:texcoord` published on the imported
cube), renders four settle frames after the predicate first holds so the
readback observes a frame that already carries the awaited state, and bounds
the loop with both a 96-frame cap and a 15 s `std::chrono` budget. Exhausting
either exits cleanly and the existing UV assertion reports the probe's exit
summary (`frames=… ready=never …`) rather than the test dying on a ctest
timeout with no diagnosis.

Every assertion from `e1416f08` is restored verbatim: 8 V / 18 E / 36 H / 12 F
with `h:texcoord` and no `v:texcoord`, the GPU record's 24 vertices and 36
surface indices with non-zero vertex/texcoord/normal BDAs, the pass statuses,
and the center-pixel-versus-three-background-corners readback. None was
weakened, relabelled, or removed, and no production code changed.

### Result

| | Before | After |
| --- | --- | --- |
| Display off (1 Hz present) | 97.78 s standalone; cohort timeout at 30 s | **6.8 s** (6.79 / 6.83 / 6.81) |
| Display on | ~13 s | **3.7 s** (3.70 / 3.68 / 3.70) |

- Residual, recorded not dismissed: the same present throttle applies to every
  other smoke in the cohort. They survive it only because they run four frames
  (`kTargetFrames`), which costs ~4 s rather than ~96 s. The sibling
  `PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan` noted at 25.5 s
  on the original host runs at 5.8 s here, so it is not currently at risk, but
  a cohort-wide headroom review under a throttled display is not part of this
  task.
- Completion commit: this retirement commit.

## Goal
- Land the `BUG-137` slice B `Operational` readback smoke so that a seam-split
  corner-UV mesh is proven to render on a real Vulkan backend, inside the
  30 s per-test budget the `gpu;vulkan` cohort actually enforces.

## Non-goals
- No weakening or deletion of the readback assertions to fit the budget.
- No `slow` label on `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`; that
  label is applied per executable and would drop the whole cohort out of the
  required gate.
- No change to the corner-UV upload behaviour itself, which is already covered
  on CPU by `Test.CornerTexcoordUpload` and `Test.MeshGeometryExtraction`.

## Context
- The smoke was authored, run green on hardware, and committed as `e1416f08`,
  then reverted from `sandbox-workflow-audit-fixes` on 2026-08-07 because it
  times out in the gate.
- Evidence, NVIDIA GeForce RTX 4090, driver 580.159.04, `ci-vulkan` preset
  (combined ASan+UBSan):
    - First standalone runs: 12.7 s, 12.9 s — comfortably green.
    - Full `-L gpu -L vulkan` gate: `***Timeout 30.14 s`, reproduced on a
      second, uncontended gate run (`***Timeout 30.13 s`).
    - A later standalone run of the identical binary and filter: **33.9 s**.
- So this is not a ctest-environment artifact: the test's own wall clock varies
  between roughly 13 s and 34 s, straddling the 30 s limit that
  `tests/CMakeLists.txt` applies to every non-`slow` executable
  (`_intrinsic_default_test_timeout_seconds`).
- Reducing the frame budget does **not** help and measured *worse* (16 frames
  → 18 s versus 96 frames → 13 s): cutting the loop short moves the async
  enrichment and generated-normal bake into shutdown, which then blocks.
  Composing without `TextureBakeModule` (32 frames) still timed out.
- The variance source is therefore not yet identified. Suspects, in the order
  worth ruling out: swapchain/present pacing in `ExitAfterFramesApp` under a
  non-VSync config; the deferred direct-mesh enrichment job's wake latency; and
  first-use pipeline/shader compilation attributed to whichever test runs first.
- Note `RuntimeSandboxAcceptanceGpuSmoke.PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan`
  already runs at 25.5 s in the same cohort, so the headroom problem is not
  unique to this test and a shared diagnosis may cover both.

## Required changes
- [x] Diagnose the 13 s → 34 s variance rather than padding the budget around
      it (`intrinsicengine-diagnose`: deterministic loop, ranked hypotheses,
      tagged probes).
- [x] Restore the smoke from `e1416f08` — the test body and the
      `ReadSurfaceGeometryRecordByEntityId` helper are both in that commit.
- [x] Keep its assertions intact: ECS mesh 8 V / 18 E / 36 H / 12 F carrying
      `h:texcoord` and no `v:texcoord`, GPU geometry record reporting 24
      vertices and 36 surface indices, and a center pixel distinguishable from
      three background corners.

## Tests
- [x] The restored smoke passes under
      `ctest --test-dir build/ci-vulkan -L gpu -L vulkan` with the cohort's
      default timeout, on three consecutive runs (54/54 each; the smoke itself
      6.81 s / 6.83 s / 3.76 s).
- [x] Default CPU gate stays green (4142/4142 with the expected
      environment-gated GLFW/LSan skip).

## Docs
- [x] If the fix changes the smoke authoring pattern (frame budgeting, bootstrap
      choice), record it in `docs/agent/` and re-sync
      `intrinsicengine-gpu-smoke-authoring`. The "a frame count is not a time
      budget" rule is now in that skill, whose `SKILL.md` is canonical (it has
      no generated `references/`); `sync_skills.py --check` is clean.

## Acceptance criteria
- [x] `BUG-137` slice B closes `Operational` with a cited, in-budget gate run.
- [x] No readback assertion was weakened to get there.
- [x] The `gpu;vulkan` gate has no new timeout or flake.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R 'SeamSplitCornerUv' -L 'gpu' -L 'vulkan'
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan'
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Relaxing, skipping, or deleting a readback assertion to reach green.
- Labelling the smoke `flaky-quarantine` without a recorded diagnosis.
- Raising `_intrinsic_default_test_timeout_seconds` for the whole tree to
  accommodate one test.

## Maturity
- Target: `Operational` on Vulkan-capable hosts. The corner-UV upload path is
  already `CPUContracted` and stays that way while this is open.
