---
id: PROC-013
theme: H
depends_on: []
---
# PROC-013 — Knowledge-graph discovery aid (graphify adapters + MCP)

## Goal
- Provide an optional, deterministic, queryable knowledge graph of the engine
  (C++23 module DAG + paper → method → code chain) for agent navigation and
  review, rendered/served by [graphify](https://github.com/safishamsi/graphify)
  but built from this repository's own authoritative parsers so it needs no LLM
  backend and produces 100%-`EXTRACTED` edges.

## Non-goals
- Becoming an authority. The layering gate stays `check_layering.py`; paper
  claims stay in method contracts (`method.yaml` + `docs/methods/*`). This graph
  is a discovery aid only and is not consulted by any gate or CI check.
- Forking graphify's tree-sitter C++ extractor (it cannot parse C++20/23 module
  syntax; we feed it correct edges instead).
- Committing generated graph artifacts (they are `build/` output).
- Wiring graphify's probabilistic PDF/LLM concept extraction into any gate.

## Context
- graphify's native extractor skips `.cppm` files and cannot resolve C++23
  module imports, so its out-of-the-box graph misses the engine's entire
  dependency structure. Rather than fork it, the adapters reuse
  `generate_module_inventory` (`export module` parsing) and `check_layering`
  (import extraction, layer ownership, `ALLOWED_DEPS`).
- Method packages already carry `method.yaml` + `paper.md` + `src/include`, so a
  paper → method → code graph is derivable deterministically; `paper.md` section
  headings give "paper concept" nodes without any PDF/LLM step.
- The merged graph is served to agents in-session via the repo `.mcp.json`
  (`knowledge-graph` server → `graphify-mcp`), exposing `query_graph`,
  `get_neighbors`, `shortest_path`, `god_nodes`, `graph_stats`, etc.
- Owner/layer: `tools/repo` + agent workflow. Satisfies the `tools/**`
  docs-sync rule via `tools/repo/README.md` and this task.

## Required changes
- [x] `tools/repo/export_module_graph.py` — module DAG → graphify JSON.
- [x] `tools/repo/export_method_graph.py` — paper → method → code → graphify JSON.
- [x] `tools/repo/build_knowledge_graph.py` — orchestrator + in-Python merge.
- [x] `.mcp.json` — register the `knowledge-graph` MCP server (opt-in; requires
      `uv tool install graphifyy --with mcp`).
- [x] `tools/repo/README.md` — document the workflow, MCP setup, and the
      optional PDF layer.
- [ ] (follow-up) Light up `method → engine-module` edges once a reference
      method gains an optimized/GPU backend that imports engine modules.
- [ ] (follow-up) Optional CI smoke that builds the graph and asserts node/edge
      floors, only if the aid graduates from optional to relied-upon.

## Tests
- [x] `python3 tools/repo/build_knowledge_graph.py` builds the merged graph.
- [x] `graphify-mcp --graph <artifact>` completes the MCP `initialize` handshake
      and lists graph-query tools.
- [ ] (follow-up) Fixture-based regression test for the adapters (mirroring
      `tests/regression/tooling/Test.CheckLayering.py`) if this becomes load-bearing.

## Docs
- [x] `tools/repo/README.md` knowledge-graph section (workflow + MCP + PDF note).

## Acceptance criteria
- [x] The graph builds deterministically with no API key and no graphify install.
- [x] Engine module edges are correct where graphify's native extractor produces
      none (`.cppm` modules resolved).
- [x] Generated artifacts are gitignored; no graph JSON/HTML is committed.
- [x] `check_layering.py`/paper contracts remain the sole authorities; this aid
      gates nothing.

## Verification
```bash
python3 tools/repo/build_knowledge_graph.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Letting the discovery graph become an authority for layering or paper claims.

## Maturity
- Target: this slice closes at **Scaffolded** — an optional discovery aid with
  no gate dependency. No Operational follow-up is owed; the unchecked items are
  genuinely optional and only become relevant if the aid is later promoted to a
  relied-upon check.
