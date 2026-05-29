"""CLI: python -m coscientist.run --goal examples/poisson_progressive.json
       python -m coscientist.run --task demo            # zero-setup smoke run
Flags override the goal file. Default is fully offline (no API key, no GPU)."""
from __future__ import annotations
import argparse
from .config import RunConfig
from .orchestrator import run


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--goal", help="path to a goal JSON (see examples/)")
    ap.add_argument("--task", help="task plugin name (overrides goal file)")
    ap.add_argument("--rounds", type=int)
    ap.add_argument("--live", action="store_true",
                    help="real LLM agents + real harness (needs API key / --harness)")
    ap.add_argument("--harness", help="harness command, e.g. ./build/poisson_harness")
    ap.add_argument("--workdir")
    a = ap.parse_args()

    cfg = RunConfig.from_json(a.goal) if a.goal else RunConfig()
    if a.task:
        cfg.task = a.task
    if a.rounds is not None:
        cfg.rounds = a.rounds
    if a.live:
        cfg.offline = False
    if a.harness:
        cfg.harness_cmd = a.harness
        cfg.offline = False
    if a.workdir:
        cfg.workdir = a.workdir
    run(cfg)


if __name__ == "__main__":
    main()
