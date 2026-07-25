# IntrinsicEngine Research Artifact

This Agent-Native Research Artifact records research-significant process events for IntrinsicEngine work sessions.

## Layers

- `logic/` records crystallized claims, constraints, architecture notes, and heuristics:
  - `logic/problem.md` — the research problem statement.
  - `logic/claims.md` — the claim ledger (`C<NN>`).
  - `logic/solution/constraints.md` — crystallized constraints (`K<NN>`).
  - `logic/solution/architecture.md` — crystallized architecture statements (`A<NN>`).
  - `logic/solution/heuristics.md` — crystallized heuristics (`H<NN>`).
- `trace/` records the research journey: `trace/exploration_tree.yaml` (`N<NN>` nodes),
  `trace/pm_reasoning_log.yaml`, and `trace/sessions/` with `trace/sessions/session_index.yaml`.
- `evidence/` records raw proof references, indexed by `evidence/README.md`.
- `staging/` records observations awaiting closure (`O<NN>`) in `staging/observations.yaml`.

## Notes

Implementation context lives in the engine tree (src/, methods/, tests/) and is bound to a claim
through its `Proof` field, not mirrored under `ara/`.

`python3 tools/agents/check_ara_claims.py --root . --strict` validates the layer index above and
the claim ledger. The authoritative policy is `docs/agent/ara-evidence-policy.md`.
