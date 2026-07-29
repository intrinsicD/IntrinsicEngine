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

## Anti-patterns

- Promoting a `hypothesis` to `supported` without adding the artifact that justifies it.
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
