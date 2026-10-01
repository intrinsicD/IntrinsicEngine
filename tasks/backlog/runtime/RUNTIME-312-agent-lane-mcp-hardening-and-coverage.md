---
id: RUNTIME-312
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive multi-slice follow-up of the 2026-10-01 MCP lane review; evidence is the diff, contract/regression tests, review and CI.
contract_schema: 1
contracts: [method.engine-integration, repo.source-documentation]
---
# RUNTIME-312 — Agent control lane: MCP hardening, conformance and tool coverage

## Goal
Make the MCP agent lane (`tools/agents/mcp_bridge.py`, `Runtime.AgentServer`,
`Runtime.AgentOperations*`) dependable for long and minimized sessions,
conformant with MCP 2025-06-18, consistent across its tools, and complete for
the editor operations the Sandbox UI already offers.

## Context
- Review 2026-10-01 (operator request) of the lane built by ARCH-019,
  RUNTIME-287, RUNTIME-288 and PROC-035. Canonical description:
  [agent control lane](../../../docs/architecture/agent-control-lane.md),
  ADR 0029.
- Findings this task owns:
  1. A minimized Sandbox never reaches `FramePhase::UiBuild`
     (`Runtime.Engine.cpp`, minimized branch of the platform phase), so every
     tool call stalls until the bridge's 120 s socket timeout drops the
     connection and discards pending replies.
  2. Continuation tools (`run_registration`, `run_kmeans`, captures, ...) that
     outlast the bridge timeout lose their reply although the job keeps running
     and publishes. The bridge is single-threaded: while a call waits it neither
     answers client `ping` nor reads `notifications/cancelled`, and it discards
     server messages that are not the awaited reply.
  3. Server and bridge echo any client `protocolVersion` instead of negotiating
     a supported one.
  4. Results are JSON text only; no `structuredContent`/`outputSchema`.
  5. No `notifications/progress` for long operations; `notifications/cancelled`
     is ignored. Job cancellation itself is owned by
     [RUNTIME-279](RUNTIME-279-editor-job-snapshot-and-cancel.md).
  6. `destructiveHint` is `false` for every tool; `view_capture` can overwrite
     an existing file outside the undo history.
  7. Without a running Sandbox the client sees only `sandbox_status` and must
     call it by hand; the bridge never discovers a Sandbox that starts later.
  8. Inconsistent tool surface: `run_kmeans` / `run_point_cloud_consolidation`
     take `entity`/`domain` arguments (free string vs. enum) while the other
     `run_*` tools read config sections; keypoints, k-means and consolidation
     have no `preview_*`; their registrations do not follow the file's
     formatting.
  9. Editor commands with UI but no agent tool: mesh curvature, geodesics,
     curvature segmentation, descriptor analysis, kernel density, density
     weight, normal estimation, outlier analysis, point spacing, point
     construction, parameterization, progressive Poisson, bilateral filter,
     mesh denoise/remesh/simplify/subdivide, scalar ridge, scene save/load,
     entity visibility and camera pose. `import_file` only answers `Pending`.
  10. `agent-control-lane.md` is stale: its Naming list misses
      `preview_point_sampling`, `run_keypoint_analysis`, `run_kmeans` and
      `run_point_cloud_consolidation`, and Limitations still says
      "mesh-field operations only".
  11. The `.mcp.json` `knowledge-graph` entry fails with ENOENT when
      `graphify-mcp` is not installed; nothing tells the operator why.
- Constraints kept from ADR 0029: owner-only local socket, main-thread
  execution through existing editor commands, `Agent: ` history labels, no
  generic scene/property writes, UI parity first (a tool calls the same runtime
  function as its panel). Bridge stays standard-library only.
- Frame-loop ordering, minimized/idle waits and shutdown follow
  `intrinsicengine-sandbox-input-lifecycle`; right-size new surfaces with
  `intrinsicengine-right-sizing` (prefer one table-driven configured-operation
  tool over twenty near-identical registrations).

## Slice plan
1. **Docs and environment truth (mechanical).** Correct finding 10 in
   `agent-control-lane.md` and `tools/agents/README.md`; document in
   `tools/repo/README.md` / the lane doc how a missing `graphify-mcp` shows up
   and how to install it (finding 11). No code.
2. **Minimized and long-running sessions (server).** Agent calls make progress
   while the window is minimized: run the agent drain on minimized frames
   through the narrowest engine seam, while capture tools fail fast with a typed
   "viewport not presentable" error instead of waiting. A deferred reply stays
   owned by its connection until it completes, independent of bridge timeouts.
3. **Bridge concurrency and discovery.** Replace the blocking request loop with
   a `select`-based loop over stdin and the socket: answer client `ping` during
   calls, forward server notifications, apply the timeout per call without
   dropping the connection for a still-running job (report it as an error reply
   and discard the late reply by id), and probe periodically while disconnected
   so a Sandbox started later is announced by `notifications/tools/list_changed`
   without a manual `sandbox_status`.
4. **Protocol conformance.** Version negotiation against an explicit supported
   list in server and bridge; `structuredContent` (plus `outputSchema` where the
   result shape is stable) alongside the existing text content; correct
   annotations (`destructiveHint` for anything not undoable); `view_capture`
   refuses to overwrite an existing file unless `overwrite: true`.
5. **Progress and cancellation.** For continuation tools whose request carries
   `_meta.progressToken`, emit `notifications/progress` from the job snapshot
   each frame (rate-limited). `notifications/cancelled` drops the pending
   continuation and, once RUNTIME-279 exposes `Cancel`, cancels the editor job;
   until then the reply states that the job continues.
6. **Consistent tool surface.** One shared domain/positions schema fragment;
   `preview_*` for keypoint analysis, k-means and consolidation using the same
   runtime readiness as their panels; decide (and record here) whether entity
   and domain come from config sections or arguments for all point families;
   bring the three registrations to the file's formatting.
7. **Coverage of existing editor commands.** A table-driven
   `preview_operation` / `run_operation` pair over the configured editor
   commands of finding 9 (operation enum → existing `Preview*`/`ApplyEditorConfigured*`
   path, continuation for `Pending`), migrating `run_mesh_operation` into it
   without breaking its name; plus `save_scene`/`load_scene` (allowed roots),
   `set_visibility`, `set_camera` (pose or preset, same path as the camera
   controller command), and an `import_file` `wait` option that answers once the
   import materialized or failed.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged per method; each tool uses the property refs its existing editor command and config section already accept (any compatible element domain, never narrowed to vertices). |
| Compatible entity sources | Every entity source the corresponding panel/runtime preflight accepts; the agent adds none and removes none. |
| RuntimeModule | Existing method modules and editor command surfaces only; `Runtime.AgentServer` stays the sole agent transport owner. |
| Config/agent | This task owns the agent bindings (slices 6–7) through the existing config sections and `Preview*`/`ApplyEditorConfigured*` paths with `RuntimeConfigControlSource::AgentCli`. |
| UI | No new UI; every tool mirrors an existing panel action. A missing UI action is a prerequisite for its tool, not part of this task. |
| Publication | Unchanged: the commands' own same-domain publication, atomic history entry and `Agent: ` label. |
| End-to-end tests | `Test.AgentOperations.cpp` per new tool (readiness, apply, undo), `Test.SandboxAgentServer.cpp` socket round trips (minimized, progress, cancel, long job), `Test.McpBridge.py` for the bridge. |

## Acceptance criteria
- [x] Slice 1: lane doc Naming/Limitations match the registered tools; the knowledge-graph prerequisite and its failure symptom are documented.
- [x] Slice 2: a tool call to a minimized Sandbox completes (non-capture) or fails fast with a typed error (capture); a deferred reply outliving 120 s is still delivered to its connection.
- [ ] Slice 3: the bridge answers `ping` during a pending call, forwards server notifications, keeps the connection after a per-call timeout, and announces a later-started Sandbox via `tools/list_changed` without a manual call.
- [ ] Slice 4: unsupported `protocolVersion` gets the server's own version; tool results carry `structuredContent`; annotations match undoability; `view_capture` never overwrites without `overwrite: true`.
- [x] Slice 5: progress notifications arrive for a long continuation tool with a progress token; cancellation drops the pending reply (the job itself keeps running: cancelling it waits for RUNTIME-279, documented in `agent-control-lane.md`).
- [ ] Slice 6: keypoint, k-means and consolidation have `preview_*` tools and one shared argument convention recorded in this note.
- [ ] Slice 7: every command of finding 9 is reachable through an agent tool with undo coverage where it edits the scene; `run_mesh_operation` keeps working.
- [ ] `agent-control-lane.md`, `tools/agents/README.md` and the module inventory are current after every slice.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AgentOperations|SandboxAgentServer|SandboxEditorSessionLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
python3 tests/regression/tooling/Test.McpBridge.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root .
```
