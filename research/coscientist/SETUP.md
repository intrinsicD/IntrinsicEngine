# SETUP.md — set up the co-scientist in the IntrinsicEngine repo

Give this to Claude Code **inside the engine repo**. It sets up the task-unspecific framework
(provided, already working) and implements only the pieces that need the engine.

## Guiding principle: reuse the repo, add the minimum

The `coscientist/` package is complete and domain-agnostic — drop it in unchanged. The only
real new code is the **harness host** (a thin CLI wrapper around the engine's *existing*
Vulkan/dispatch/shader/registry infrastructure) and **task plugins** (a scorer + goal per
target). Do not duplicate engine infrastructure, do not edit the `coscientist/` core, and do
not create parallel build/venv setups when the repo already has them.

## Step 1 — place the package, reuse the environment

- Copy `coscientist/` and `tasks/` into the repo under a sensible home (e.g. `research/coscientist/`).
- Add deps to the **existing** Python environment / requirements only if missing:
  `anthropic` (live runs only), and `numpy` + `scipy` (only the Poisson example task needs
  these; the core needs neither). If the repo has no Python env yet, create one venv here.
- Do not restructure the repo around this; it is a tool that lives beside the engine.

## Step 2 — verify the core works untouched (offline, no API key, no GPU)

```bash
python -m coscientist.run --task demo
python -m coscientist.run --goal examples/poisson_progressive.json
```

Expected: the demo task's evolved high-objective candidates rise to the top of the
leaderboard; the Poisson task ranks dart-throwing (bridson) above jittered-grid, with
white-noise (gate-failed) at the bottom. If both hold, the orchestration, tournament, Elo,
evolution and meta-review are sound and you can move to the engine.

## Step 3 — implement the harness host (the only real engine code)

Follow `HARNESS_HOST_TASK.md` exactly. The harness is a standalone CLI that builds and runs
one untrusted candidate headless, dumps a point set, and emits the harness-result JSON in
`contract.md`. **Reuse, don't rebuild:**

- reuse the engine's existing **headless Vulkan** init (or the minimal subset);
- reuse the existing **shader compilation** path (GLSL/HLSL → SPIR-V);
- reuse the existing **compute dispatch + buffer readback**;
- reuse the existing **algorithm-registry** interface for `cpp_module` candidates;
- add it as a new **CMake target** (`poisson_harness`) beside the engine — no new build system.

The harness contributes only: the CLI, a *fixed trusted host pipeline* that the candidate
shader is swapped into, child-process isolation, the hard timeout, and device-lost handling.

Acceptance (from the brief): run the engine's existing, proven Poisson sampler through the
harness; the Poisson task's scorer must report `gate.passed = true` and a clean blue-noise
spectrum. Broken candidates must be contained (gate fail, host survives) and busy-loops
killed by the timeout.

## Step 4 — go live for Poisson

```bash
export ANTHROPIC_API_KEY=...
python -m coscientist.run --goal examples/poisson_progressive.json \
    --live --harness ./build/poisson_harness
```

(`--live` + `--harness` flip off offline mode and point the harness layer at your binary.)

## Step 5 — add a new target as a task plugin (no core changes)

To research another algorithm (e.g. anisotropic Kuwahara, XDoG/FDoG edge quality, hatching):

1. Copy `tasks/poisson/` to `tasks/<name>/` as a template.
2. Implement `Task.score(cfg, artifact)` using the engine's **existing rendering** to produce
   the candidate's output and an appropriate metric — for NPR filters, a perceptual image
   metric (FLIP or SSIM) against a fixed reference render, plus any hard validity gate.
3. Implement `harness_args(cfg)` for that target's harness inputs, and either reuse the same
   harness host (dispatch by manifest) or add a sibling target to it.
4. Write `examples/<name>.json` with `"task": "<name>"` and the target's `task_params`.
5. Run offline first (provide offline stubs), then `--live`.

## Boundaries (do not cross)

- Never edit `coscientist/` to support a new target — write a `Task`.
- Never duplicate engine infra in the harness — call into it.
- Never run auto-generated GPU candidates on a workstation you rely on — use a separate or
  remote GPU box; record that in the run config / CI, not as a manual habit.
- Keep the trusted scorer separate from candidate code (the design already enforces this;
  don't route candidate code through `score()`).

## Acceptance checklist

- [ ] `python -m coscientist.run --task demo` sorts evolved candidates to the top.
- [ ] `python -m coscientist.run --goal examples/poisson_progressive.json` ranks bridson > grid > white-noise.
- [ ] `poisson_harness` builds as a CMake target reusing existing engine infra.
- [ ] Existing reference Poisson sampler through the harness → scorer reports gate PASS + clean spectrum.
- [ ] Broken-candidate and timeout acceptance tests pass without crashing the host.
- [ ] `--live --harness ./build/poisson_harness` completes a multi-round run and writes overview.json.
