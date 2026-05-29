"""The supervisor loop -- task-unspecific. Synchronous by design (add an async worker pool
only once a single goal works end to end). One round:

  generate -> run+score -> reflect -> tournament -> evolve(next) -> meta-review

Reduced to the components the paper's ablations show carry the weight: Reflection-with-search,
a benchmark-grounded tournament, Evolution driven by measured failures, and Meta-review
feedback appended to prompts next round."""
from __future__ import annotations
import json, os
from .config import ModelConfig
from .llm import LLM
from .state import Store
from .task import load_task
from . import agents, harness, tournament


def run(cfg):
    cfg.models = ModelConfig()
    task = load_task(cfg.task)
    weights = cfg.fitness_weights or task.default_fitness_weights()

    os.makedirs(os.path.join(cfg.workdir, cfg.goal_id), exist_ok=True)
    store = Store(os.path.join(cfg.workdir, cfg.goal_id, "state.sqlite"))
    llm = LLM()
    if not cfg.offline and not llm.available():
        raise RuntimeError("offline=False but anthropic SDK/API key unavailable")

    carried, meta, pending = [], "", []

    for rnd in range(cfg.rounds):
        print(f"\n=== round {rnd}  (task={task.name}) ===")

        new = agents.generate(llm, cfg, store, rnd, meta, task) + pending
        pending = []

        for h in new:
            rec = harness.run_candidate(cfg, task, h["manifest"])
            fit = tournament.fitness(rec, weights)
            review = agents.reflect(llm, cfg, h["manifest"], rec, meta, task)
            store.set_result(h["id"], rec, fit, rec["gate"].get("passed"), review)
            gate = "PASS" if rec["gate"].get("passed") else "fail"
            print(f"  {h['id']} [{h['manifest'].get('summary','')}] gate={gate} fitness={fit}")

        active = list(dict.fromkeys([h["id"] for h in new] + carried))
        hmap = {i: {"id": i, "manifest": store.hypothesis(i)["manifest"]} for i in active}
        for a, b in tournament.schedule_pairs(active):
            tournament.resolve_match(llm, cfg, store, hmap[a], hmap[b], task, rnd)

        lb = store.leaderboard()
        carried = [r["id"] for r in lb[:cfg.evolve_top_k]]
        top_h = [{"id": r["id"], "manifest": store.hypothesis(r["id"])["manifest"]}
                 for r in lb[:cfg.evolve_top_k]]
        pending = agents.evolve(llm, cfg, store, rnd + 1, top_h, meta, task)

        summaries = []
        for h in new:
            rec = store.record(h["id"]) or {}
            summaries.append(f"{h['manifest'].get('summary','')}: "
                             f"gate={rec.get('gate',{}).get('passed')} "
                             f"quality={rec.get('quality')}")
        meta = agents.meta_review(llm, cfg, summaries, task)
        store.add_meta(rnd, meta)

    lb = store.leaderboard()
    overview = {"goal": cfg.research_goal, "goal_id": cfg.goal_id, "task": task.name,
                "rounds": cfg.rounds, "leaderboard": lb,
                "top": [{"id": r["id"],
                         "summary": store.hypothesis(r["id"])["manifest"].get("summary"),
                         "elo": r["elo"], "fitness": r["fitness"],
                         "record": store.record(r["id"])} for r in lb[:5]],
                "final_lessons": meta}
    out_path = os.path.join(cfg.workdir, cfg.goal_id, "overview.json")
    with open(out_path, "w") as f:
        json.dump(overview, f, indent=2)

    print("\n=== final leaderboard ===")
    for r in lb:
        print(f"  {r['id']}  elo={r['elo']:.0f}  gate={r['gate_passed']}  "
              f"fitness={r['fitness']}  "
              f"{store.hypothesis(r['id'])['manifest'].get('summary','')}")
    print(f"\noverview written to {out_path}")
    return overview
