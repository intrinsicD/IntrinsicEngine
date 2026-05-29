"""Task-unspecific run configuration. Domain-specific knobs go in `task_params`, which the
selected task interprets. `fitness_weights` empty -> the task's defaults are used."""
from __future__ import annotations
import os, json
from dataclasses import dataclass, field


@dataclass
class ModelConfig:
    generation: str = os.environ.get("CS_MODEL_GEN", "claude-opus-4-8")
    reflection: str = os.environ.get("CS_MODEL_REF", "claude-opus-4-8")
    ranking: str    = os.environ.get("CS_MODEL_RANK", "claude-sonnet-4-6")
    evolution: str  = os.environ.get("CS_MODEL_EVO", "claude-sonnet-4-6")
    meta: str       = os.environ.get("CS_MODEL_META", "claude-sonnet-4-6")


@dataclass
class RunConfig:
    goal_id: str = "demo_v1"
    research_goal: str = "Maximize the demo objective."
    candidate_kind: str = "shader"           # "shader" | "cpp_module"

    task: str = "demo"                       # task plugin name or dotted path
    task_params: dict = field(default_factory=dict)   # interpreted by the task

    # loop budget
    rounds: int = 6
    candidates_per_round: int = 4
    evolve_top_k: int = 2

    # fitness weights applied to flattened numeric metrics; empty -> task defaults
    fitness_weights: dict = field(default_factory=dict)

    # execution
    offline: bool = True                     # True -> stub agents + mock harness, no API/GPU
    harness_cmd: str = ""                    # e.g. "./build/poisson_harness"; empty -> mock
    harness_timeout_ms: int = 15000
    workdir: str = "cs_runs"

    @classmethod
    def from_json(cls, path: str) -> "RunConfig":
        with open(path) as f:
            return cls(**json.load(f))
