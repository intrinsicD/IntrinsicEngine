"""Elo tournament where the match winner is decided by MEASURED benchmark when both
candidates passed the gate (ground truth), falling back to the Ranking agent's debate
otherwise. Fully task-unspecific: fitness flattens whatever numeric metrics the task's
scorer returned and applies the configured weights."""
from __future__ import annotations
import itertools


def _flatten_numeric(d, out):
    for k, v in d.items():
        if isinstance(v, dict):
            _flatten_numeric(v, out)
        elif isinstance(v, bool):
            continue
        elif isinstance(v, (int, float)):
            out[k] = v


def fitness(record, weights):
    """Scalar fitness from a result record; None if the gate failed. Only relative order
    matters (it decides a pairwise match), so raw weighted metrics are fine."""
    if not record or not record.get("gate", {}).get("passed"):
        return None
    flat = {}
    for sec in ("quality", "metrics", "performance"):
        if isinstance(record.get(sec), dict):
            _flatten_numeric(record[sec], flat)
    return sum(w * flat.get(k, 0.0) for k, w in weights.items())


def _expected(ra, rb):
    return 1.0 / (1.0 + 10 ** ((rb - ra) / 400.0))


def update_elo(store, a, b, winner, k=32):
    ra, rb = store.elo(a), store.elo(b)
    sa = 1.0 if winner == a else 0.0
    ea = _expected(ra, rb)
    store.set_elo(a, ra + k * (sa - ea))
    store.set_elo(b, rb + k * ((1 - sa) - (1 - ea)))


def resolve_match(llm, cfg, store, a_h, b_h, task, rnd):
    a, b = a_h["id"], b_h["id"]
    fa, fb = store.fitness(a), store.fitness(b)
    ea, eb = store.is_eligible(a), store.is_eligible(b)
    if ea and eb and fa is not None and fb is not None:
        winner, kind = (a if fa >= fb else b), "benchmark"
    elif ea and not eb:
        winner, kind = a, "gate"
    elif eb and not ea:
        winner, kind = b, "gate"
    else:
        from . import agents
        choice = agents.rank_debate(llm, cfg, a_h, b_h, task)
        winner, kind = (a if choice == "a" else b), "debate"
    update_elo(store, a, b, winner)
    store.record_match(a, b, winner, kind, rnd)
    return winner


def schedule_pairs(active_ids):
    """Round-robin among the round's active set. Swap in proximity-aware pairing later."""
    return list(itertools.combinations(active_ids, 2))
