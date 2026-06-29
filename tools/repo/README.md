# tools/repo

Repository structure and policy scripts.

## Current scripts

- `check_root_hygiene.py`: reports root-level markdown and allowed/blocked status; local build/vendor/IDE artifact directories are ignored when comparing the root allowlist.
- `check_expected_top_level.py`: compares current top-level source tree entries to configured expectations; local build/vendor/IDE artifact directories are ignored.
- `check_layering.py`: validates layer dependency boundaries in warning
  mode by default, with `--strict` for CI enforcement. Covers
  ``#include`` directives, C++23 module imports (including promoted
  ``Extrinsic.<Layer>.*`` prefixes), and CMake
  ``target_link_libraries(...)`` edges between promoted targets. Use
  ``--exclude PATTERN`` to skip fixture or generated paths. Fixture cases
  live under ``tests/contract/repo/layering_fixtures/`` and are exercised
  by ``tests/regression/tooling/Test.CheckLayering.py``.
- `check_ui_contract_guard.sh`: UI boundary guard script (canonical path).
- `check_layering_allowlist_quality.py`: validates layering allowlist entry hygiene (required metadata, duplicate keys, broad legacy wildcard bans, and open task-owner references).
- `check_test_layout.py`: enforces taxonomy-owned test source layout and forbids legacy wrapper test directories.
- `generate_module_inventory.py`: module inventory generator for `src/`; defaults to `docs/api/generated/module_inventory.md`.
- `export_module_graph.py`: emits a [graphify](https://github.com/safishamsi/graphify)-schema `graph.json` for the C++23 module dependency graph, reusing this directory's own module/layering parsers (graphify's tree-sitter C++ extractor cannot parse module syntax). Edges are tagged `same-layer`/`allowed`/`violation`; the authoritative gate remains `check_layering.py`.
- `export_method_graph.py`: emits a graphify-schema `graph.json` for the paper → method → code chain from `methods/` (`method.yaml`, `paper.md` headings, method sources). Engine-module import edges reuse `export_module_graph`'s ids so the two graphs merge.
- `build_knowledge_graph.py`: orchestrator — runs both adapters and merges them (in pure Python, no graphify/API key needed) into `build/knowledge-graph/graphify-out/graph.json`.

## Knowledge graph (optional agent discovery aid)

A deterministic, queryable map of the engine for navigation/review — **not** an
authority (layering is gated by `check_layering.py`; paper claims live in method
contracts). Generated output is a `build/` artifact and is not committed.

```bash
# 1. Build the merged code + method/paper graph (deterministic, no API key).
python3 tools/repo/build_knowledge_graph.py

# 1b. Optional all-agent setup wrapper. Use --no-install to skip graphify.
tools/setup/provision_knowledge_graph.sh --no-install

# 2a. Visualize / query from the CLI (requires: uv tool install graphifyy).
cd build/knowledge-graph/graphify-out && graphify cluster-only . --no-label
graphify explain "Extrinsic.Core.Logging" --graph build/knowledge-graph/graphify-out/graph.json

# 2b. Query live in-session over MCP. The repo `.mcp.json` registers a
#     `knowledge-graph` server (query_graph / get_neighbors / shortest_path /
#     god_nodes / graph_stats). Requires the MCP extra:
uv tool install graphifyy --with mcp
```

Shared session setup lives under `tools/setup/`. Agent-specific hooks should
wrap those scripts rather than duplicating provisioning logic; for example,
`.claude/setup.sh` delegates to `tools/setup/agent_session_setup.sh --async-json`.

Optional probabilistic layer (not wired): `graphify add <paper.pdf>` with an LLM
backend key builds a fuzzy concept graph from a real PDF that can be merged in
with `graphify merge-graphs`. `paper.md` is the no-key deterministic alternative
the method adapter already consumes.

## Config files

- `layering_allowlist.yaml`: temporary path-scoped exceptions for `check_layering.py`; each entry must include task and expiry notes, avoid broad `src/legacy/**` wildcards, and point at an open removal owner. Current legacy `Interface` rows point at `LEGACY-001`; the remaining legacy subtree rows point at `LEGACY-002` until that task seeds per-subtree deletion owners.

## Compatibility entrypoints

To avoid breaking historical docs/scripts during migration, legacy wrappers are temporarily retained at:

- `tools/check_ui_contract_guard.sh`
- `tools/repo/generate_module_inventory.py`

These wrappers should be removed in the compatibility cleanup phase (RORG-112).
