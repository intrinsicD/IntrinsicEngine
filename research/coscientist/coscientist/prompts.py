"""Task-unspecific prompt library. The experiment specifics come from the task via
`experiment_summary`, so these prompts work for any geometry/rendering target. The
meta-review feedback string is prepended to Generation/Reflection/Evolution each round."""
from __future__ import annotations


def _goal(cfg, experiment: str) -> str:
    s = (f"RESEARCH GOAL: {cfg.research_goal}\n"
         f"Candidate kind: {cfg.candidate_kind}\n")
    if experiment:
        s += f"Fixed experiment: {experiment}\n"
    return s


GEN_SYS = (
    "You are the Generation agent of a co-scientist for geometry-processing and rendering "
    "algorithms. You propose NOVEL, implementable candidate algorithms grounded in the "
    "literature. Each candidate is real source the harness will compile and run, so it must "
    "be concrete and self-contained. Prefer mechanisms that plausibly improve the target "
    "metric over restating known methods.")


def gen_user(cfg, experiment, meta, n):
    s = _goal(cfg, experiment)
    if meta:
        s += f"\nLessons from previous rounds (heed these):\n{meta}\n"
    s += (f"\nPropose {n} distinct candidate algorithms. For each, return an object with: "
          '"summary" (one line), "rationale" (why it should improve the metric, cite ideas), '
          '"source" (complete source: a compute/fragment shader for kind=shader, or a C++ '
          "module implementing the engine's algorithm-registry interface for kind=cpp_module), "
          'and "params" (small JSON dict of tunables). '
          "Reply ONLY with a JSON array of these objects, no prose.")
    return s


REF_SYS = (
    "You are the Reflection agent: a rigorous peer reviewer. Use web search to assess whether "
    "the candidate's core idea is genuinely novel and whether its reasoning is correct. Do not "
    "rate something novel if prior work already proposed it. Be concise and decisive.")


def ref_user(cfg, experiment, manifest, result, meta):
    s = _goal(cfg, experiment)
    if meta:
        s += f"\nKnown recurring issues to check for:\n{meta}\n"
    s += (f"\nCandidate summary: {manifest.get('summary','')}\n"
          f"Rationale: {manifest.get('rationale','')}\n")
    if result:
        s += (f"\nMeasured result -> gate_passed={result.get('gate',{}).get('passed')}, "
              f"quality={result.get('quality')}, perf={result.get('performance')}\n")
    s += ('\nReturn ONLY a JSON object: {"novel": bool, "correct": bool, '
          '"novelty_score": 0-10, "issues": [strings], "verdict": one short sentence}.')
    return s


RANK_SYS = (
    "You are the Ranking agent. Compare two candidate algorithms in a short scientific debate "
    "focused on novelty, correctness and likelihood of improving the target metric, then pick "
    "the better one. Be impartial to ordering.")


def rank_user(cfg, experiment, a, b):
    return (_goal(cfg, experiment) +
            f"\nCandidate A: {a.get('summary','')} | rationale: {a.get('rationale','')}\n"
            f"Candidate B: {b.get('summary','')} | rationale: {b.get('rationale','')}\n"
            '\nDebate briefly, then reply ONLY with JSON: {"winner": "A" or "B", '
            '"reason": one sentence}.')


EVO_SYS = (
    "You are the Evolution agent. You refine top candidates into NEW, better ones; you never "
    "modify a candidate in place -- each refinement competes fresh in the tournament. Use the "
    "measured results (failure logs, metrics) to target real weaknesses.")


def evo_user(cfg, experiment, parents, meta, n):
    s = _goal(cfg, experiment)
    if meta:
        s += f"\nLessons from previous rounds:\n{meta}\n"
    s += "\nTop candidates and their measured results:\n"
    for p in parents:
        rec = p["record"]
        s += (f"- {p['manifest'].get('summary','')}: gate="
               f"{rec.get('gate',{}).get('passed')}, quality={rec.get('quality')}, "
               f"log tail: {str(rec.get('build',{}).get('log',''))[:200]}\n")
    s += (f"\nProduce {n} refined candidates addressing the observed weaknesses. Same JSON "
          "array schema as Generation. Strategies: literature grounding, combining two "
          "parents, simplification, or an out-of-the-box divergent idea. Reply ONLY with JSON.")
    return s


META_SYS = (
    "You are the Meta-review agent. Synthesize recurring patterns across this round's reviews "
    "and results into terse, actionable guidance that will be prepended to the other agents "
    "next round. Output guidance only, no preamble.")


def meta_user(cfg, experiment, summaries):
    return (_goal(cfg, experiment) +
            "\nThis round's candidate summaries, reviews and results:\n" +
            "\n".join(summaries) +
            "\n\nWrite at most 6 bullet lessons. Be specific and corrective.")
