---
id: PROC-013
theme: H
depends_on: []
---
# PROC-013 — Knowledge-graph discovery aid (graphify adapters + shared setup)

## Goal
- Provide an optional, deterministic, queryable knowledge graph of the engine
  (C++23 module DAG + paper → method → code chain) for agent navigation and
  review, rendered/served by [graphify](https://github.com/safishamsi/graphify)
  but built from this repository's own authoritative parsers so it needs no LLM
  backend and produces 100%-`EXTRACTED` edges.
- Make the optional knowledge-graph/session provisioning path available to all
  agents through shared `tools/setup/` scripts instead of a Claude-only hook.

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
- The first session-start provisioning path lived only in `.claude/setup.sh`,
  which made graphify install/build convenient for Claude but invisible to
  Codex, Copilot, and bare API clients.
- Owner/layer: `tools/repo`, `tools/setup`, and agent workflow. Satisfies the
  `tools/**` docs-sync rule via `tools/repo/README.md`, `AGENTS.md`,
  `docs/agent/contract.md`, and this task.

## Required changes
- [x] `tools/repo/export_module_graph.py` — module DAG → graphify JSON.
- [x] `tools/repo/export_method_graph.py` — paper → method → code → graphify JSON.
- [x] `tools/repo/build_knowledge_graph.py` — orchestrator + in-Python merge.
- [x] `.mcp.json` — register the `knowledge-graph` MCP server (opt-in; requires
      `uv tool install graphifyy --with mcp`).
- [x] `tools/repo/README.md` — document the workflow, MCP setup, and the
      optional PDF layer.
- [x] `tools/setup/provision_knowledge_graph.sh` — shared optional graphify
      installer/graph builder, nonfatal when called from session setup.
- [x] `tools/setup/agent_session_setup.sh` — shared all-agent session setup
      script that can run foreground for humans/agents or emit hook async JSON.
- [x] `tools/setup/wait_for_agent_setup.sh` — shared wait helper for agents
      that need the toolchain before running CMake gates.
- [x] `.claude/setup.sh` and `.claude/wait-for-toolchain.sh` — reduce to thin
      adapters around the shared scripts.
- [ ] (follow-up) Light up `method → engine-module` edges once a reference
      method gains an optimized/GPU backend that imports engine modules.
- [ ] (follow-up) Optional CI smoke that builds the graph and asserts node/edge
      floors, only if the aid graduates from optional to relied-upon.

## Tests
- [x] `python3 tools/repo/build_knowledge_graph.py` builds the merged graph.
- [x] `graphify-mcp --graph <artifact>` completes the MCP `initialize` handshake
      and lists graph-query tools.
- [x] Shell-parse the shared setup scripts and Claude wrappers with `bash -n`.
- [x] Run the shared knowledge-graph provisioning script through graph build at
      least once in this workspace.
- [ ] (follow-up) Fixture-based regression test for the adapters (mirroring
      `tests/regression/tooling/Test.CheckLayering.py`) if this becomes load-bearing.

## Docs
- [x] `tools/repo/README.md` knowledge-graph section (workflow + MCP + PDF note).
- [x] `AGENTS.md` and `docs/agent/contract.md` describe the shared optional
      session setup path.
- [x] Regenerate agent skill mirrors after editing `docs/agent/contract.md`.

## Acceptance criteria
- [x] The graph builds deterministically with no API key and no graphify install.
- [x] Engine module edges are correct where graphify's native extractor produces
      none (`.cppm` modules resolved).
- [x] Generated artifacts are gitignored; no graph JSON/HTML is committed.
- [x] `check_layering.py`/paper contracts remain the sole authorities; this aid
      gates nothing.
- [x] Knowledge-graph setup is not Claude-only; shared scripts are under
      `tools/setup/` and Claude files are wrappers.
- [x] System package installation remains explicit session provisioning
      behavior; knowledge-graph setup remains optional and nonfatal from the
      session hook.

## Verification
```bash
python3 tools/repo/build_knowledge_graph.py
bash tools/setup/provision_knowledge_graph.sh --no-install
bash -n tools/setup/agent_session_setup.sh tools/setup/provision_knowledge_graph.sh tools/setup/wait_for_agent_setup.sh .claude/setup.sh .claude/wait-for-toolchain.sh
tools/setup/wait_for_agent_setup.sh --timeout 1
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Letting the discovery graph become an authority for layering or paper claims.

## Maturity
- Target: this task remains **Scaffolded** as an optional discovery aid with no
  gate dependency. No `Operational` follow-up is owed unless a later task
  promotes the graph from convenience tooling to a relied-upon check.
