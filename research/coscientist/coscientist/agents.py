"""Specialized agents (Generation / Reflection / Ranking / Evolution / Meta-review). Each has
a real (LLM) path and an offline path. The offline path delegates domain content to the task,
so the core stays task-unspecific. Selected by cfg.offline."""
from __future__ import annotations
import itertools
from . import prompts

_counter = itertools.count(1)


def _cid() -> str:
    return f"cand_{next(_counter):04d}"


def generate(llm, cfg, store, rnd, meta, task):
    n = cfg.candidates_per_round
    if cfg.offline:
        raw = task.offline_candidates(cfg, n, cfg.candidate_kind)
    else:
        out = llm.call(cfg.models.generation, prompts.GEN_SYS,
                       prompts.gen_user(cfg, task.experiment_summary(cfg), meta, n),
                       web_search=True, max_tokens=8000)
        raw = llm.extract_json(out) or []
    return [_register(store, cfg, m, rnd, "generation") for m in raw]


def reflect(llm, cfg, manifest, result, meta, task):
    if cfg.offline:
        passed = result.get("gate", {}).get("passed")
        return {"novel": True, "correct": bool(passed), "novelty_score": 5,
                "issues": [] if passed else ["fails correctness gate"],
                "verdict": "ok" if passed else "invalid"}
    out = llm.call(cfg.models.reflection, prompts.REF_SYS,
                   prompts.ref_user(cfg, task.experiment_summary(cfg), manifest, result, meta),
                   web_search=True)
    return llm.extract_json(out) or {"novel": None, "correct": None, "issues": []}


def rank_debate(llm, cfg, a_h, b_h, task) -> str:
    """Return 'a' or 'b'. Used only when the benchmark cannot decide a match."""
    if cfg.offline:
        return "a"
    out = llm.call(cfg.models.ranking, prompts.RANK_SYS,
                   prompts.rank_user(cfg, task.experiment_summary(cfg),
                                     a_h["manifest"], b_h["manifest"]))
    j = llm.extract_json(out) or {}
    return "b" if str(j.get("winner", "A")).upper() == "B" else "a"


def evolve(llm, cfg, store, rnd, parents, meta, task):
    k = cfg.evolve_top_k
    if cfg.offline:
        raw = task.offline_evolve(cfg, parents, k)
        return [_register(store, cfg, m, rnd, "evolution",
                          parent=parents[i]["id"] if i < len(parents) else None)
                for i, m in enumerate(raw)]
    pinfo = [{"manifest": p["manifest"], "record": store.record(p["id"]) or {}}
             for p in parents[:k]]
    out = llm.call(cfg.models.evolution, prompts.EVO_SYS,
                   prompts.evo_user(cfg, task.experiment_summary(cfg), pinfo, meta, k),
                   max_tokens=8000)
    raw = llm.extract_json(out) or []
    return [_register(store, cfg, m, rnd, "evolution",
                      parent=parents[0]["id"] if parents else None) for m in raw]


def meta_review(llm, cfg, summaries, task) -> str:
    if cfg.offline:
        return task.offline_meta(cfg)
    return llm.call(cfg.models.meta, prompts.META_SYS,
                    prompts.meta_user(cfg, task.experiment_summary(cfg), summaries))


def _register(store, cfg, m, rnd, agent, parent=None):
    cid = _cid()
    manifest = {
        "id": cid, "goal_id": cfg.goal_id, "kind": cfg.candidate_kind,
        "summary": m.get("summary", ""), "rationale": m.get("rationale", ""),
        "source": m.get("source", ""), "params": m.get("params", {}),
        "provenance": {"agent": agent, "parent": parent, "round": rnd},
    }
    store.add_hypothesis({"id": cid, "goal_id": cfg.goal_id, "kind": cfg.candidate_kind,
                          "text": m.get("summary", ""), "manifest": manifest,
                          "parent": parent, "agent": agent, "round": rnd})
    return {"id": cid, "manifest": manifest}
