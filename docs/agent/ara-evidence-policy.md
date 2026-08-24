# ARA evidence and claim policy

`ara/` is the Agent-Native Research Artifact: the repository's claim and evidence ledger. This
document is the authoritative policy for it. Read it when a change would state a research,
performance, parity, or capability result, when closing a method or benchmark task, or when
promoting a claim into `README.md`, `docs/`, a method report, or a task status line.

`python3 tools/agents/check_ara_claims.py --root . --strict` enforces the structural half of this
policy and runs in `ci-docs.yml`. It cannot judge whether a sentence overstates its artifact —
that is the `intrinsicengine-results-audit` skill.

## Why the ledger exists

The engine contract already requires benchmarks to use declared manifests and baselines
(`AGENTS.md` §8) and forbids claiming a performance win without a baseline comparison. The ledger
is the layer above that: it records *which statements this repository is willing to stand behind*,
what would falsify each one, and exactly which artifact backs it. A number in a commit message is
not a claim. A row in `ara/logic/claims.md` is.

This matters most for negative and bounded results. A `refuted` claim with a clean falsification
record is worth more than a vague positive, because it stops the next agent from re-running work
that has already failed a gate.

## Layout

| Path | Holds | IDs |
|---|---|---|
| `ara/PAPER.md` | layer index; start here | — |
| `ara/logic/problem.md` | research problem statement | — |
| `ara/logic/claims.md` | the claim ledger | `C<NN>` |
| `ara/logic/solution/constraints.md` | crystallized constraints | `K<NN>` |
| `ara/logic/solution/architecture.md` | crystallized architecture statements | `A<NN>` |
| `ara/logic/solution/heuristics.md` | crystallized heuristics | `H<NN>` |
| `ara/staging/observations.yaml` | observations awaiting closure | `O<NN>` |
| `ara/trace/exploration_tree.yaml` | the research journey | `N<NN>` |
| `ara/trace/sessions/` | per-session records, indexed by `session_index.yaml` | — |
| `ara/evidence/` | raw proof references and tables | — |

## Claim record format

Every claim is a `## C<NN>: <title>` section carrying nine fields:

```markdown
## C07: <one-line title>
- **Statement**: What is claimed, scoped to the protocol that produced it.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: What observation would overturn this.
- **Proof**: [tests/unit/geometry/Test.Foo.cpp,
  tasks/done/METHOD-0NN-foo.md,
  ara/evidence/tables/foo_cohort.md]
- **Dependencies**: [K14]
- **Tags**: geometry, CPU reference
- **From staging**: O30
```

- **`Status`** starts with a disposition word — `supported`, `refuted`, `hypothesis`, `untested`,
  `unavailable`, `superseded`, `withdrawn` — and may carry a scope qualifier after it. A
  `supported` or `refuted` claim must cite at least one repository path in `Proof` that exists on
  disk; a `hypothesis` may cite only an exploration node and the task that would test it.
- **`Proof`** entries are repository paths (`src/`, `tests/`, `tasks/`, `methods/`, `docs/`,
  `benchmarks/`, `tools/`, `ara/`), `N<NN>` exploration nodes, or a commit hash. Paths are checked
  for existence, so moving a cited file means updating the claim in the same change.
- **`Dependencies`** resolve by prefix: `C` in the claim ledger, `K` in `constraints.md`, `A` in
  `architecture.md`, `H` in `heuristics.md`. A dependency on an ID nobody crystallized is an error.
- **`From staging`** must name an `O<NN>` that exists in `staging/observations.yaml`.

## When to write to the ledger

| Situation | Action |
|---|---|
| A method/benchmark task produced a result worth repeating | Add a `C<NN>` row bound to its artifact |
| A gate rejected the hypothesis | Add a `refuted` row — do not silently drop the work |
| An observation is interesting but not yet decided | Add an `O<NN>` to `staging/observations.yaml` |
| A claim's evidence moved or was superseded | Update `Proof`, or add a superseding claim |
| Closing a task at `ParityProven` or `Operational` | Check whether the maturity statement is a claim |
| A number is about to enter README/docs/a method report | It needs a row first |

Aligns with the maturity taxonomy in `docs/agent/task-maturity.md`: a task closing at
`Scaffolded` or `CPUContracted` rarely owns a claim, while `Operational` and `ParityProven`
usually do, because both assert observed behavior.

## Running an evidence campaign (the experiment loop)

The ledger records outcomes; this loop produces them. It applies whenever an
experiment probes behavior not fully known in advance — a novel formulation,
regime behavior on real data, numerics beyond a paper's reported envelope.
This is the scientist's loop: maintain an explicit, testable model of the
partially-known system and spend experiments where they discriminate. Paper
reproduction against reported results needs only the method workflow; the more
the ground truth is already known, the less of this loop is owed.

1. **Model explicitly.** State the current working model of the system as
   beliefs with status — established (`K`/`H` rows, `supported` claims),
   `hypothesis`, unknown. The ARA is that model; keep it diffable rather than
   implicit in session context.
2. **Predict, then run.** Before executing an experiment, record what the
   working model predicts (the `intrinsicengine-research-ideation`
   killing-experiment fields: null hypothesis, signature if correct, signature
   under the strongest conventional explanation). A run without a recorded
   prediction cannot surprise you — it can only be rationalized afterwards.
3. **Append, never curate.** Every executed experiment lands in the campaign
   record — predicted vs. observed, including failed, contradictory, and null
   runs. A sweep may share one `O<NN>`; what may never happen is dropping a
   run because it contradicted the hypothesis.
4. **A surprise voids the plan.** An observation that contradicts the working
   model stops the remaining experiment plan — it was designed to discriminate
   under a model that just failed. Revise the model, then re-derive which
   experiment is now most informative.
5. **Prefer discriminating experiments.** When several explanations survive,
   run the cheapest experiment whose possible outcomes separate them, not the
   one most likely to confirm the favorite. A negative result rejects only the
   configuration actually tested; record that scope with it.
6. **Backtest before promoting.** `hypothesis → supported` requires the claim
   to be consistent with *every* observation of the campaign, not only the
   confirming run. A claim that contradicts a staged `O<NN>` stays
   `hypothesis` — or becomes `refuted` — until the contradiction is resolved.
7. **When nothing survives, indict the instrument.** If no explanation fits
   the record, the next hypothesis list must question the campaign's own
   representation: the metric, the dataset regime, a measurement or leakage
   bug, the formulation itself. Route suspected measurement bugs through
   `intrinsicengine-diagnose` — its observation ledger is this same discipline
   at bug scale.

## Anti-patterns

- Promoting a `hypothesis` to `supported` without adding the artifact that justifies it.
- Promoting `hypothesis` to `supported` on the confirming run alone while a staged observation
  contradicts it. Backtest the claim against the campaign's full observation record first.
- Citing a `build/` or otherwise untracked path as proof — a fresh clone cannot resolve it.
- Rewriting or deleting a `refuted` claim to tidy the ledger. Dispositions are append-only in
  spirit: supersede, do not erase.
- Recording a benchmark number in a claim without the manifest and baseline the benchmarking
  protocol requires (`AGENTS.md` §8, `docs/agent/benchmark-workflow.md`).
- Treating a generated task report, canonical benchmark result, or accepted
  bundle audit as automatic claim authorization. Claim eligibility is explicit;
  claim-grade work also owes its frozen protocol, source/data/config seals,
  independent audit, and this ledger row.
- Hiding failed cells, excluded rows, abandoned attempts, or superseded
  protocols while presenting only the successful confirmation path.
- Treating green CPU CI as evidence for GPU or Vulkan behavior. Those are distinct evidence
  classes; say which one a claim rests on.
