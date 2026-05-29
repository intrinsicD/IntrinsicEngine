"""Task plugin interface -- the boundary between the task-unspecific core and a research
target. A Task supplies only the domain-specific pieces the generic loop needs:

  * score()              the trusted fitness function (gate + quality on an artifact)
  * experiment_summary() how to describe the fixed experiment to the agents (prompts)
  * harness_args()       the task-specific CLI args its harness host expects
  * default_fitness_weights()
  * offline_*()          stubs so the whole loop runs with no API key and no GPU

Everything else -- agents, tournament, Elo, state, orchestration -- is task-unspecific and
lives in the core package. Add a new target by writing a Task subclass; never edit the core.

A task module must expose `TASK = <YourTask>()`. Load it by short name (a module under
`tasks/`) or by dotted import path.
"""
from __future__ import annotations
import importlib


class Task:
    name = "base"

    # ---- required ----
    def score(self, cfg, artifact_path: str) -> dict:
        """Trusted scorer. Read the artifact the harness produced and return
        {"gate": {"passed": bool, ...}, "quality": {<numeric metrics>} | None}.
        Quality must be None (not computed) when the gate fails. NEVER run candidate
        code here -- this is the trusted judge and must stay separate from candidates."""
        raise NotImplementedError

    # ---- prompt / fitness wiring ----
    def experiment_summary(self, cfg) -> str:
        """One-line description of the fixed experiment, injected into agent prompts."""
        return ""

    def default_fitness_weights(self) -> dict:
        """Weights applied to flattened numeric metrics; cfg.fitness_weights overrides."""
        return {}

    # ---- real harness ----
    def harness_args(self, cfg) -> list[str]:
        """Extra CLI args appended after the always-present --manifest/--points/--out/
        --timeout-ms when invoking cfg.harness_cmd."""
        return []

    # ---- offline stubs (no API / no GPU); optional but enable smoke testing ----
    def offline_candidates(self, cfg, n: int, kind: str) -> list[dict]:
        raise NotImplementedError("this task has no offline generation stub")

    def offline_evolve(self, cfg, parents: list, n: int) -> list[dict]:
        return []

    def offline_mock_run(self, cfg, manifest: dict, artifact_path: str) -> dict:
        raise NotImplementedError("this task has no mock harness")

    def offline_meta(self, cfg) -> str:
        return "Prefer candidates that pass the gate; avoid repeating approaches that failed."


def load_task(name: str) -> Task:
    """Resolve a task by short name (under tasks/) or dotted module path.
    The module must define TASK."""
    tried = [name, f"tasks.{name}", f"tasks.{name}.task"]
    last = None
    for mod in tried:
        try:
            m = importlib.import_module(mod)
        except ImportError as e:
            last = e
            continue
        if hasattr(m, "TASK"):
            return m.TASK
    raise ImportError(f"could not load task '{name}' (tried {tried}): {last}")
