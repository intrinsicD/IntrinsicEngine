# tools/repo

Repository structure and policy scripts.

## Current scripts

- `check_root_hygiene.py`: enforces root-level Markdown policy and the shared
  repository-owned root policy, reporting allowed roots, named ignored local
  state, unexpected entries, and missing required entries separately.
- `check_expected_top_level.py`: compatibility entrypoint for
  `check_root_hygiene.py`; the hygiene wrapper invokes only the canonical
  checker so the root is not scanned twice.
- `check_layering.py`: validates layer dependency boundaries in warning
  mode by default, with `--strict` for CI enforcement. Covers
  ``#include`` directives, C++23 module imports (including promoted
  ``Extrinsic.<Layer>.*`` prefixes), and CMake
  ``target_link_libraries(...)`` edges between promoted targets. Use
  ``--exclude PATTERN`` to skip fixture or generated paths. Fixture cases
  live under ``tests/contract/repo/layering_fixtures/`` and are exercised
  by ``tests/regression/tooling/Test.CheckLayering.py``.
- `check_kernel_convergence.py`: enforces the `ARCH-014` no-backsliding
  snapshot for `Runtime.Engine.cppm`. It counts domain imports as the explicit
  substrate-allowlist complement, tracks re-exports and public `GetX` names,
  and fails on both new and stale policy entries so improvements ratchet in the
  same change. Synthetic regressions live in
  `tests/regression/tooling/Test.CheckKernelConvergence.py`.
- `check_ui_contract_guard.sh`: UI boundary guard script (canonical path).
- `check_layering_allowlist_quality.py`: validates layering allowlist entry hygiene (required metadata, duplicate keys, broad legacy wildcard bans, and open task-owner references).
- `check_test_layout.py`: enforces taxonomy-owned test source layout and forbids legacy wrapper test directories.
- `check_shader_outputs.py`: validates that a shader compilation output tree contains the expected SPIR-V files; used by shader build/compile tasks to fail closed on an empty or partial compile. Not wired into a workflow gate.
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

- `layering_allowlist.yaml`: temporary path-scoped exceptions for `check_layering.py`; each entry must include task and expiry notes, avoid broad wildcards, and point at an open removal owner. The allowlist is currently empty.
- `root_allowlist.yaml`: the exact expected repository roots and bounded named
  patterns for disposable local build/dependency/editor/tool state consumed by
  both root checkers. The scripts do not consult developer-global Git ignore
  configuration, so it cannot hide an unowned source root.
- `kernel_convergence_policy.json`: versioned exact snapshot, substrate
  classification, and temporary-debt ownership consumed by
  `check_kernel_convergence.py`. `RUNTIME-178` owns the recorded debt.

## Compatibility entrypoints

- `tools/check_ui_contract_guard.sh` (root-level) is a surviving compatibility wrapper that delegates to the canonical `tools/repo/check_ui_contract_guard.sh`. The root-path move wrappers from the reorganization (RORG-071/RORG-112) are otherwise removed; this one entrypoint remains for historical callers.
