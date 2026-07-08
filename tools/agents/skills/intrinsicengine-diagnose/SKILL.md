---
name: intrinsicengine-diagnose
description: Disciplined diagnosis loop for hard bugs and performance regressions in IntrinsicEngine. Build a deterministic feedback loop → reproduce → rank 3-5 hypotheses → instrument with tagged probes → fix → regression-test → cleanup. Use whenever the user says "diagnose this", "debug this", reports a crash, validation-layer error, CPU/null gate failure, parity mismatch between reference and optimized/GPU backend, hot-reload regression, or a benchmark regression versus baseline.
---

# IntrinsicEngine Diagnose

A discipline for hard bugs and performance regressions in a C++23 + Vulkan
research engine. Skip phases only when explicitly justified — the loop exists
because shortcuts here are how bad fixes ship.

Before exploring code, load the layering vocabulary from `intrinsicengine-core`
and any relevant ADRs under `docs/adr/`. Bugs in a layered engine almost always
have a layer; name it early.

Two domain playbooks narrow this loop for recurring failure classes — route
there first when they match:

- Unexplained SEGV/ASan/vtable/ICE failure, especially after `.cppm` module
  changes → `intrinsicengine-stale-build-triage` (rule out stale BMIs before
  diagnosing).
- Black/wrong frame, validation (VUID) cascade, or driver crash on the
  promoted Vulkan path → `intrinsicengine-vulkan-frame-triage`.

## Phase 1 — Build a feedback loop

**This is the skill.** Everything else is mechanical. With a fast, deterministic,
agent-runnable pass/fail signal for the bug, bisection and hypothesis testing
just consume that signal. Without one, no amount of staring at `.cppm` files
will save you.

Spend disproportionate effort here. Be aggressive, be creative, refuse to give up.

### Construction order — try in roughly this sequence

1. **Failing ctest target** at the right label — `unit`, `contract`,
   `integration`, `regression`, `gpu`, `vulkan`. Prefer the narrowest label that
   reaches the bug. `ctest -L <label> -R <pattern>` is your friend.
2. **Backend differential.** For correctness bugs in a method or pipeline,
   run the same input through the reference backend and the optimized/GPU
   backend; diff the outputs. This is the canonical IntrinsicEngine debug
   technique — the reference backend *is* the oracle.
3. **Replay a captured frame / dataset.** Save the input mesh, point cloud,
   image, or RHI command stream to disk; replay through the buggy path in
   isolation. Avoids depending on full `Engine::Run()` to reproduce.
4. **Throwaway harness.** A `tests/integration/<area>/repro_<bug>.cpp` that
   stands up only the modules needed (one RHI device, one method, mocked
   asset service) to call into the bug path. Delete or absorb when done.
5. **Validation layers.** For Vulkan bugs, the loop is "run with
   `VK_LAYER_KHRONOS_validation` enabled and grep the log for the first
   `VALIDATION` line". The first message is the bug; later messages are
   cascades.
6. **Benchmark differential.** For perf regressions, run the relevant
   `benchmarks/` runner at the suspected-good commit and the suspected-bad
   commit and diff the result JSON's metrics block. See
   `intrinsicengine-benchmark`.
7. **Bisection harness.** If the bug appeared between two known commits,
   wrap the feedback loop in a script that `git bisect run` can call.
8. **Property / fuzz loop.** For "sometimes wrong output" in geometry or
   numerical code, run N random inputs through reference vs. optimized
   and assert deltas stay within method-declared tolerances.

Build the right feedback loop, and the bug is 90% fixed.

### Iterate on the loop itself

Treat the loop as a product. Once you have *a* loop, ask:

- Can I make it faster? (Use focused CMake targets, skip unrelated `add_subdirectory`,
  shrink the input dataset to the minimal case.)
- Can I make the signal sharper? (Assert on the specific symptom — exact pixel
  delta, exact ULP mismatch, exact validation-layer message — not "didn't crash".)
- Can I make it more deterministic? (Pin random seeds, freeze the asset
  manifest, run with a fixed thread count, disable async loaders, fix the
  frame index.)

A 30-second flaky loop is barely better than no loop. A 2-second deterministic
loop is a debugging superpower.

### Non-deterministic bugs

For race conditions in the task graph, async loaders, or the GPU queue, the
goal is not a clean repro but a **higher reproduction rate**. Loop the trigger
100×, run the test under `ctest --repeat until-fail:100 -R <name>` (or the
equivalent `ctest --repeat-until-fail 100 -R <name>` — both require an
iteration count; bare `--repeat until-fail` is invalid and exits immediately),
run with TSan, narrow timing windows with `std::this_thread::sleep_for`
injection. A 50%-flake bug is debuggable;
1% is not — keep raising the rate until it's debuggable.

### When you genuinely cannot build a loop

Stop and say so explicitly. List what you tried. Ask the user for: (a) access
to a host that reproduces it (Vulkan-capable GPU, specific driver version),
(b) a captured artifact (validation-layer log, RenderDoc capture, frame
profiler trace, core dump, asset that triggers it), or (c) permission to add
temporary instrumentation in a follow-up task. Do **not** proceed to
hypothesise without a loop.

Do not proceed to Phase 2 until you have a loop you believe in.

## Phase 2 — Reproduce

Run the loop. Watch the bug appear.

Confirm:

- [ ] The loop produces the failure mode the **user** described — not a
      different validation error or assertion that happens to be nearby.
      Wrong bug = wrong fix.
- [ ] The failure is reproducible across multiple runs (or for non-deterministic
      bugs, at a high enough rate to debug against).
- [ ] You captured the exact symptom (validation message, parity delta value,
      crash backtrace, benchmark metric delta) so later phases can verify the
      fix actually addresses it.

Do not proceed until you reproduce the bug.

## Phase 3 — Hypothesise

Generate **3–5 ranked hypotheses** before testing any of them. Single-hypothesis
generation anchors on the first plausible idea, and the first plausible idea in
a Vulkan codebase is wrong more often than not.

Each hypothesis must be **falsifiable** — state the prediction it makes.

> Format: "If `<X>` is the cause, then `<changing Y>` will make the bug
> disappear, or `<changing Z>` will make it worse."

If you cannot state the prediction, the hypothesis is a vibe — discard or
sharpen it.

**Show the ranked list to the user before testing.** They often have domain
knowledge that re-ranks instantly ("we just changed the GpuScene push-constant
layout") or know hypotheses they've already ruled out. Cheap checkpoint, big
time saver. Don't block on it — proceed with your ranking if the user is AFK.

## Phase 4 — Instrument

Each probe must map to a specific prediction from Phase 3. **Change one
variable at a time.**

Tool preference:

1. **Debugger / `gdb` / `lldb`** if the env supports it. One breakpoint beats
   ten log lines.
2. **RenderDoc / GPU validation** for graphics bugs. One frame capture beats
   ten print statements over a Vulkan API.
3. **Targeted logs** at the boundaries that distinguish hypotheses (layer
   boundaries are the right places — RHI ↔ vulkan, AssetService ↔ graphics,
   ECS ↔ runtime).
4. Never "log everything and grep".

**Tag every debug log** with a unique prefix, e.g. `[DBG-a4f2]`. Cleanup at
the end becomes a single `git grep '\[DBG-'`. Untagged logs survive; tagged
logs die.

**Perf branch.** For benchmark regressions, logs are usually wrong. Instead:
establish a baseline measurement using the existing benchmark manifest's
runner, then bisect. Measure first, fix second. Re-read
`intrinsicengine-benchmark` for baseline-comparison policy and warmup rules
before claiming a "fix".

## Phase 5 — Fix + regression test

Write the regression test **before the fix** — but only if there is a
**correct seam** for it.

A correct seam exercises the **real bug pattern** as it occurs at the call
site. If the only available seam is too shallow (a unit test on a pure
function when the bug needs the whole method pipeline; a contract test when
the bug only manifests on the Vulkan backend), a regression test there gives
false confidence.

**If no correct seam exists, that itself is the finding.** Note it. The
codebase architecture is preventing the bug from being locked down — flag this
for the post-mortem and consider it grounds for an architecture review (see
`intrinsicengine-review`).

If a correct seam exists:

1. Turn the minimised repro into a failing test at that seam with the right
   ctest label (`regression` is usually correct; `vulkan` if it's
   backend-specific; `gpu` if it requires a GPU host).
2. Watch it fail with `ctest -R <name>`.
3. Apply the fix.
4. Watch it pass.
5. Re-run the Phase 1 feedback loop against the original (un-minimised) scenario.

For correctness bugs in a method, follow the method backend policy: the
reference backend is canonical truth — if reference and optimized/GPU
disagree, the bug is in the optimized/GPU backend unless you can prove
otherwise. Do not "fix" the reference to match a buggy optimized output.

## Phase 6 — Cleanup + post-mortem

Required before declaring done:

- [ ] Original repro no longer reproduces (re-run the Phase 1 loop).
- [ ] Regression test passes under the right ctest label (or the absence of a
      correct seam is documented in the post-mortem).
- [ ] All `[DBG-...]` instrumentation removed (`git grep '\[DBG-'`).
- [ ] Throwaway harness deleted, or moved to a clearly-marked location with
      a TODO and a follow-up task ID.
- [ ] No leftover `printf`, `std::cout`, or `fmt::print` debug calls.
- [ ] The hypothesis that turned out correct is stated in the commit/PR message
      — so the next debugger learns. Also note which hypotheses were ruled out
      and how.
- [ ] If the fix touched architecture-impacting code, the architecture review
      checklist is run (see `intrinsicengine-review`).

**Then ask: what would have prevented this bug?**

- A missing CPU contract test → add one and cite the task.
- A missing parity check between reference and optimized backend → add one;
  re-read `intrinsicengine-method` for the parity-delta diagnostic policy.
- A missing benchmark or baseline → add one; see `intrinsicengine-benchmark`.
- An architectural smell (no correct test seam, tangled callers, hidden
  coupling across layers) → file a follow-up architecture-review task and link
  it from the post-mortem. Make the recommendation **after** the fix is in,
  not before — you have more information now than when you started.

## Anti-patterns specific to this codebase

- **Fixing the reference backend to match the optimized backend.** The
  reference is the oracle. If they disagree, the optimized backend is wrong
  unless you can prove the reference is wrong against a third source (the
  paper, a published implementation, a hand-derived ground truth).
- **Declaring a perf regression "fixed" by re-running once.** Benchmark
  variance is real. Cite the baseline JSON, warmup config, and N-of-runs.
- **Patching around a Vulkan validation message instead of reading it.** The
  first validation message is the bug; the rest are cascades.
- **Calling the bug fixed when only the CPU/null gate is green.** That's
  `CPUContracted`, not `Operational` (see `intrinsicengine-task-workflow`
  maturity taxonomy). For backend-impacting fixes, cite a backend-labeled
  run that actually executed.
