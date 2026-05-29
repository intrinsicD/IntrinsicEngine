# Co-Scientist for geometry/rendering algorithm discovery

A working reduction of Google's Co-Scientist (Nature 2026) for a domain where the experiment
is *code*: candidate algorithms are compiled and run against IntrinsicEngine, and the fitness
signal is a **measured benchmark**, not a self-play debate.

The core is **task-unspecific**. A research target is a small **task plugin** supplying the
trusted scorer, the experiment description, the harness CLI args, and offline stubs. Add a
target by writing a `Task`; never edit the core. To set this up in the engine repo, hand
`SETUP.md` to Claude Code.

Loop, per round:

```
generate -> run+score (harness -> task scorer) -> reflect -> tournament -> evolve -> meta-review
```

Match winners are decided by the **measured** result when both candidates pass the gate;
debate is only a fallback. Correctness is gated before performance, and the scorer never runs
candidate code — that separation is the core safety invariant.

## Quickstart (offline: no API key, no GPU)

```bash
pip install -r requirements.txt          # numpy/scipy only needed for the poisson task
python -m coscientist.run --task demo                          # zero domain code
python -m coscientist.run --goal examples/poisson_progressive.json
```

The demo task shows the loop climbing (evolved candidates rise). The Poisson task ranks
dart-throwing above grid, with gate-failed white-noise at the bottom — using a mock harness so
no GPU/engine is needed yet. Output lands in `cs_runs/<goal_id>/overview.json`.

## Going live

1. Implement the harness host in the engine (`HARNESS_HOST_TASK.md`) — a thin CLI over the
   engine's existing headless Vulkan / shader / dispatch / registry, not new infra.
2. `export ANTHROPIC_API_KEY=...`
3. `python -m coscientist.run --goal examples/poisson_progressive.json --live --harness ./build/poisson_harness`

Per-agent models via env (`CS_MODEL_GEN`, `CS_MODEL_REF`, `CS_MODEL_RANK`, `CS_MODEL_EVO`,
`CS_MODEL_META`).

## Layout

```
coscientist/            TASK-UNSPECIFIC CORE (do not edit per target)
  config.py             run config; task selected by name, task knobs in task_params
  task.py               Task interface + loader
  llm.py state.py prompts.py agents.py tournament.py harness.py orchestrator.py run.py
tasks/                  TASK PLUGINS (add targets here)
  demo.py               trivial, domain-free; makes the loop run with zero setup
  poisson/              reference worked example (validated scorer + mock samplers)
contract.md             orchestrator <-> harness <-> scorer JSON contract
SETUP.md                brief for Claude Code to set everything up in the engine repo
HARNESS_HOST_TASK.md    brief for Claude Code to implement the engine harness host
examples/               goal configs (demo.json, poisson_progressive.json)
```

## Adding a target

Copy `tasks/poisson/` as a template, implement `Task.score()` with the metric for your target
(e.g. FLIP/SSIM vs a reference render for an NPR filter), reusing the engine's rendering, and
add a goal JSON. The core, agents, tournament and harness layer are unchanged.
