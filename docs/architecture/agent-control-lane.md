# Agent control lane (MCP)

Status: canonical. Decision record: [ADR 0029](../adr/0029-agent-control-lane.md).

An AI agent (an MCP client such as Claude Code) can drive a **running** Sandbox
through the same validated operations as the editor UI. The lane is opt-in and
local-only.

## Using it

1. Start the Sandbox with `--agent-socket` (optional path; default
   `$XDG_RUNTIME_DIR/intrinsic-sandbox.sock`, else `/tmp/intrinsic-sandbox-<uid>.sock`).
   `--agent-readonly` exposes only read-only tools; `--agent-root <dir>`
   (repeatable, default: the working directory) bounds file arguments.
2. The MCP client launches `tools/agents/mcp_bridge.py` (`.mcp.json` entry
   `intrinsic-sandbox`). The bridge starts even without a Sandbox, offers
   `sandbox_status`, connects when the Sandbox is up and announces the Sandbox's
   tools with `notifications/tools/list_changed`.
3. **View > Agent Connection** in the Sandbox shows the socket, the connected
   client, the mode, allowed roots and call count, and disconnects the agent.

## Ownership

| Layer | Owner | Role |
| --- | --- | --- |
| platform | `Extrinsic.Platform.LocalSocket` | Owner-only (0600) Unix-domain listener and connections with timeout-bounded, non-throwing I/O; stale socket files are replaced, live owners and non-socket paths refused. |
| runtime | `Extrinsic.Runtime.AgentOperations` | Registry of named operations (`Name`, `Title`, `Description`, JSON-Schema input, `ReadOnly`, invoker); editor-backed operations in `Runtime.AgentOperations.Editor.cpp`; `ResolveAgentPath` for allowed roots. |
| runtime | `Extrinsic.Runtime.AgentServer` | `AgentProtocol` (socket-free MCP core: `initialize`, `ping`, `tools/list`, `tools/call`) and `AgentServerModule` (socket thread, request queues, frame hook, status service). |
| app | `src/app/Sandbox/main.cpp`, `Sandbox.EditorShell.cpp` | Launch flags and the Agent Connection window. |
| tools | `tools/agents/mcp_bridge.py` | Stdio MCP bridge for the client. |

## Threading and frames

The socket thread only frames newline-delimited JSON-RPC and queues messages
(at most 64 pending; more get a `-32000` busy reply; a message above 8 MiB drops
the client). The module's `UiBuild` frame hook attaches its own editor workspace
attachment on first use and handles at most `MaxCallsPerFrame` (default 4)
messages per frame on the main thread. Responses go back through the socket
thread; responses for a dropped client are discarded. Nothing exists without the
launch flag: no module, thread or socket.

## Operations and policy

- Every operation calls an existing editor query, command or config function:
  workspace snapshots, inspector property catalogs, `SelectEditorEntity`,
  `ApplyEditorFileImportCommand`, document undo/redo, `EngineConfigControl`
  preview/apply with `RuntimeConfigControlSource::AgentCli`, the mesh-field and
  registration `Preview*/Apply*` commands, the point-cloud point-sampling, keypoint,
  k-means and consolidation run commands, and the panels' Show recipe
  (`MakeEditorPropertyVisualizationRecipe`). There is no generic scene or property write.
- Naming. Read-only (`readOnlyHint`): `scene_entities`, `entity_properties`, `config_sections`,
  `config_schema`, `config_get`, `config_preview`, `history`, `jobs`, `log`,
  `preview_registration`, `preview_point_sampling`, `preview_mesh_operation` and
  `view_screenshot`. State-changing: `select_entity`, `import_file`, `show_property`,
  `config_apply`, `undo`, `redo`, `run_mesh_operation`, `run_registration` (ICP or Coherent
  Point Drift from their config sections; the reply waits for the job),
  `run_point_sampling` (the `sandbox.point_sampling` section), `run_keypoint_analysis`,
  `run_kmeans` and `run_point_cloud_consolidation`. `view_capture` writes a PNG inside the
  allowed roots.
- Screenshots complete a few frames after the call: an operation may return an
  `AgentOperationContinuation`, which the server polls each frame and answers with
  the original JSON-RPC id; a reconnecting client drops pending replies. Both tools
  call `ViewCaptureModule`, the queue behind File > Save Screenshot and F12, and
  accept a camera `preset` (restored afterwards, so `view_screenshot` stays
  read-only), `fit_entity`, and `legend_entity`, which appends a colormap strip and
  returns the property, colormap and the range the renderer uses.
- Mutating calls run under `ScopedEditorCommandLabelPrefix("Agent: ")`, so the
  undo history shows each agent change and the operator can undo it.
- Excluded by design: raw ECS or property-buffer writes, code execution, RHI
  access, runtime threading/memory/debug-layer settings, file deletion, network
  access from the engine, credentials, persisting editor settings, git, and
  quitting the Sandbox.
- UI parity: a capability a user can use is built as a Sandbox feature first;
  its tool calls the same runtime function.

## Planned capability tasks

Schemas from declarative config field tables:
[CORE-010](../../tasks/done/CORE-010-config-section-schema-export.md),
[RUNTIME-276](../../tasks/backlog/runtime/RUNTIME-276-declarative-config-field-specs.md),
[UI-057](../../tasks/done/UI-057-schema-driven-field-hints.md).
Structured readiness:
[RUNTIME-277](../../tasks/backlog/runtime/RUNTIME-277-structured-action-readiness-reasons.md),
[UI-058](../../tasks/backlog/ui/UI-058-all-reasons-readiness-tooltips.md).
Capability/UI pairs:
property inspection ([GEOM-109](../../tasks/backlog/geometry/GEOM-109-property-statistics-and-comparison.md),
[RUNTIME-278](../../tasks/backlog/runtime/RUNTIME-278-property-inspection-operations.md),
[UI-059](../../tasks/backlog/ui/UI-059-property-inspector-window.md)),
jobs ([RUNTIME-279](../../tasks/backlog/runtime/RUNTIME-279-editor-job-snapshot-and-cancel.md),
[UI-060](../../tasks/backlog/ui/UI-060-jobs-window.md)),
selection queries ([RUNTIME-280](../../tasks/backlog/runtime/RUNTIME-280-selection-query-operations.md),
[UI-061](../../tasks/backlog/ui/UI-061-select-by-query-controls.md)),
view capture ([RUNTIME-281](../../tasks/backlog/runtime/RUNTIME-281-deterministic-view-capture-command.md),
[UI-062](../../tasks/done/UI-062-save-screenshot-and-capture-controls.md)),
headless batch ([RUNTIME-282](../../tasks/backlog/runtime/RUNTIME-282-headless-batch-cli.md)),
property IO ([GEOIO-005](../../tasks/backlog/geometry/GEOIO-005-property-attributes-and-table-io.md),
[RUNTIME-283](../../tasks/backlog/runtime/RUNTIME-283-property-import-export-operations.md),
[UI-063](../../tasks/backlog/ui/UI-063-properties-import-export-window.md)),
history and checkpoints ([RUNTIME-284](../../tasks/backlog/runtime/RUNTIME-284-history-labels-and-checkpoints.md),
[UI-064](../../tasks/backlog/ui/UI-064-history-window.md)),
diagnostics ([CORE-011](../../tasks/backlog/architecture/CORE-011-log-entry-cursor-stream.md),
[RUNTIME-285](../../tasks/backlog/runtime/RUNTIME-285-diagnostics-stream.md),
[UI-065](../../tasks/backlog/ui/UI-065-diagnostics-log-window.md)),
mesh health ([GEOM-110](../../tasks/backlog/geometry/GEOM-110-connected-components-and-topology.md),
[RUNTIME-286](../../tasks/backlog/runtime/RUNTIME-286-mesh-health-report.md),
[UI-066](../../tasks/backlog/ui/UI-066-mesh-health-window.md)).
Planned: lane hardening (minimized sessions, bridge concurrency, protocol conformance,
progress and cancel) and the remaining operation tools in
[RUNTIME-312](../../tasks/backlog/runtime/RUNTIME-312-agent-lane-mcp-hardening-and-coverage.md).

## Limitations

- Operation tools exist for mesh-field operations (smoothing, spectral modes, harmonic
  field, scalar gradient), registration (ICP, Coherent Point Drift), point sampling,
  keypoint analysis, k-means and point-cloud consolidation. The remaining editor commands
  (curvature, geodesics, remeshing and others) have no tools yet; coverage is owned by
  [RUNTIME-312](../../tasks/backlog/runtime/RUNTIME-312-agent-lane-mcp-hardening-and-coverage.md).
- Imports are asynchronous (`Pending`); poll `scene_entities` for the result.
- Unix-domain sockets only; Windows builds report `Unsupported`.
- A Sandbox killed by a signal leaves its socket file; the next start replaces it.
