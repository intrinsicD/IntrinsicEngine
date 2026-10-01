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
   `sandbox_status`, probes the socket while the Sandbox is away (after
   `notifications/initialized`) and announces the Sandbox's tools with
   `notifications/tools/list_changed`. It is a single-threaded `selectors` loop:
   tool calls run concurrently with per-call timeouts (the call keeps running in
   the Sandbox), `ping` is answered locally, progress notifications are
   forwarded, `notifications/cancelled` is relayed, and MCP versions
   2025-06-18, 2025-03-26 and 2024-11-05 are negotiated.
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
the client). The module registers one drain for the `UiBuild` frame phase and one for
`FramePhase::Idle`, which the engine runs only on minimized frames (they skip every other
phase). The drain attaches its own editor workspace attachment on first use and handles at
most `MaxCallsPerFrame` (default 4) messages per frame on the main thread, so calls are served
while the window is minimized too. An Idle frame does only the command drain, event pump,
Idle hooks, job completions, pump and reap: no simulation, extraction or rendering.
Idle frames also leave out the pre-render transform flush (world matrices and bounds stay as of
the last presented frame until the window is restored), world maintenance and the frame-index
advance, and with `--agent-socket` every module's queued commands and job completions apply while
minimized. GPU work only progresses on presented frames, so tools that may dispatch it fail fast
on a minimized frame with the error code `viewport_not_presentable`: `view_screenshot`,
`view_capture`, `run_operation`, `run_mesh_operation`, `run_registration`, `run_point_sampling`,
`run_keypoint_analysis`, `run_kmeans` and `run_point_cloud_consolidation`
(`AgentOperationSpec::NeedsPresentedFrame`; any new tool that may dispatch GPU work sets it). A call of those
tools that is already waiting when the window minimizes is answered with the same code instead of
occupying a slot; its editor job or capture is not cancelled and can still finish after the
window is restored (a capture may then still write its file, so check the path or pass
`overwrite: true` when retrying). CPU-only tools, queries and the `preview_*` tools keep
working. A connection may have at most 16 deferred calls; state-changing `tools/call` requests and
read-only tools that need a presented frame (`view_screenshot`) beyond that get `-32000 too many
pending calls` before the tool runs, and other read-only tools are served unless they would defer
(then the reply is refused after they ran, which is harmless because they change nothing). A call
whose result can never arrive (an identical job was already running, or the workspace was re-
attached while it ran) ends with the error code `result_unavailable`. Responses go back through the
socket thread; responses for a dropped client are discarded.
Nothing exists without the launch flag: no module, thread or socket.

## Protocol

- Version negotiation: `initialize` echoes the client's `protocolVersion` when it is one of
  `kAgentSupportedProtocolVersions` (`2025-06-18`, `2025-03-26`, `2024-11-05`) and otherwise
  answers the newest, `2025-06-18`; the client then decides whether to continue.
- Progress: a `tools/call` with `_meta.progressToken` (string or integer) that defers its reply
  gets `notifications/progress` lines, at most one per 250 ms (`AgentServerOptions::ProgressInterval`),
  until the reply. The source is `JobService::SnapshotAll()`: the oldest Queued or Running job
  stands in for the call. A determinate job reports `progress` = percent with `total` 100;
  otherwise `progress` is the job's elapsed seconds without `total`; `message` is the job's
  debug name. With no active job, `progress` is the call's age in seconds and `message` is
  `waiting`. MCP requires `progress` to strictly increase, so a call keeps the unit chosen at its
  first notification (percent with `total`, or seconds without `total`; percent never exceeds
  100) and a notification is skipped unless its value is larger than the previous one. The
  oldest-job pick is a heuristic: several concurrent jobs make it ambiguous, and a cancelled job
  that is still running keeps feeding it. Exact per-call job tokens are future work tied to
  [RUNTIME-279](../../tasks/backlog/runtime/RUNTIME-279-editor-job-snapshot-and-cancel.md) and
  the shared progress widget ([UI-069](../../tasks/backlog/ui/UI-069-shared-operation-progress-widget.md)).
- Cancellation: `notifications/cancelled {requestId}` (JSON-typed comparison: `1` is not `"1"`)
  stops the reply and the progress of that call, but the entry stays as a tombstone that counts
  against the 16-call cap until its continuation completes, then it is dropped silently, so
  call-and-cancel cannot grow the queue. One or two progress notifications already queued may
  still arrive after the cancel (allowed by the specification). The editor job itself keeps
  running and its result still lands (undoable) until RUNTIME-279 gives the agent path a job
  cancel.
- Tool results are the existing JSON text content plus, when the negotiated version is
  `2025-06-18` or newer, `structuredContent`: the JSON object the tool returned, or
  `{"error":{"code","message"}}` for errors that carry a machine-readable `ErrorCode`
  (for example `file_exists`). Older revisions get the text content only.
- There is no `outputSchema`: it is optional in the specification and the per-tool result
  shapes are still moving, so a schema now would be a promise the tools do not yet keep.
- Annotations: `readOnlyHint` follows `ReadOnly`. `destructiveHint` (`AgentOperationSpec::Destructive`)
  is true for a mutation the undo history cannot restore and false for everything that records an
  `Agent: ` history entry: today `view_capture` (writes a file) and `config_apply` (changes engine
  configuration, which is not in the history). Mutating tools that edit the scene or its
  properties (`import_file`, `show_property`, `run_*`) are undoable, `undo`/`redo` operate on
  the history itself, and `select_entity` changes selection only, which is editor state, not scene
  data. New tools classify themselves with this rule.
- `view_capture` refuses an existing `path` unless `overwrite: true` (error code `file_exists`;
  a dangling symlink counts as occupied). The capture write repeats the check atomically
  with a hard link to a uniquely named temporary file, so a file created between the call and the
  write is never replaced; a filesystem without hard links falls back to an exclusive
  (`O_EXCL`) create written in place, which still never replaces (readers may see a partial file
  while it is written; Windows builds fail closed instead).

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
  `preview_registration`, `preview_point_sampling`, `preview_keypoint_analysis`, `preview_kmeans`,
  `preview_point_cloud_consolidation`, `preview_operation`, `preview_mesh_operation` and
  `view_screenshot`. State-changing: `select_entity`, `import_file`, `show_property`,
  `config_apply`, `undo`, `redo`, `run_operation`, `run_mesh_operation`, `run_registration` (ICP
  or Coherent Point Drift from their config sections; the reply waits for the job),
  `run_point_sampling` (the `sandbox.point_sampling` section), `run_keypoint_analysis`,
  `run_kmeans` and `run_point_cloud_consolidation`. `view_capture` writes a PNG inside the
  allowed roots.
- Configured operations. `run_operation` / `preview_operation` select a row of one table by
  `operation`: property smoothing, spectral modes, harmonic field, scalar gradient, mesh
  curvature, geodesics, curvature segmentation, normal estimation, kernel density, point
  spacing, outlier analysis, density weight, descriptor analysis, bilateral filter and point
  construction. Each row calls its panel's `Preview*`/`Apply*` path with the settings of its
  config section (`config_apply` first); the tool description lists every row with its section
  and whether the entity is an argument. A row whose section carries the entity refuses an
  `entity` argument, naming the section. `run_mesh_operation` and `preview_mesh_operation` are
  aliases limited to the four mesh-field rows. A `Pending` command answers when its job
  delivered (`result_unavailable` when none can). Geodesics has no readiness check, so its
  preview answers `"enabled": null`. The tool flag `NeedsPresentedFrame` is per tool, so
  `run_operation` (as `run_mesh_operation`) is refused while minimized even for CPU-only rows.
- Screenshots complete a few frames after the call: an operation may return an
  `AgentOperationContinuation`, which the server polls each frame and answers with
  the original JSON-RPC id; a reconnecting client drops pending replies. Both tools
  call `ViewCaptureModule`, the queue behind File > Save Screenshot and F12, and
  accept a camera `preset` (restored afterwards, so `view_screenshot` stays
  read-only), `fit_entity`, and `legend_entity`, which appends a colormap strip and
  returns the property, colormap and the range the renderer uses.
- Entity convention. A config section that carries an entity field supplies the entity (the
  tool takes none; `config_apply` first): mesh curvature, normal estimation, outlier analysis,
  kernel density, density weight, descriptor and keypoint analysis, bilateral filter, point
  spacing, point construction, point sampling and registration. Every other operation takes
  `entity` as a tool argument, as its panel takes the selection: the mesh-field operations
  (property smoothing, spectral modes, harmonic field, scalar gradient), k-means (`domain`
  needed only while `sandbox.clustering` binds no properties), consolidation (`domain`
  required), geodesics, curvature segmentation, parameterization, progressive Poisson, mesh
  topology operations and scalar ridge. The rule governs the tools added by later slices of
  RUNTIME-312 too. Every `domain` argument shares one enum generated from `GeometryElementDomain`.
- Previews. A `preview_*` tool answers `{"enabled","reason"}` with the same readiness as the
  panel's button, behind `ResolveEditorProcessingActionReadiness`; a missing service or section
  is a `false` readiness, not a call error. Known gaps: the panels also disable their run
  button while the panel's own GPU run (k-means, consolidation, keypoint transaction) awaits
  Accept, state the agent cannot see because the correlation is held by the panel; and
  `preview_point_cloud_consolidation` asks the consolidation service for the availability of
  the previewed request, which can evict the panel's two-entry readiness cache entry (the panel
  recomputes it on its next frame).
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
Planned: lane hardening (the remaining protocol conformance) and the remaining operation tools in
[RUNTIME-312](../../tasks/backlog/runtime/RUNTIME-312-agent-lane-mcp-hardening-and-coverage.md).

## Limitations

- Operation tools exist for the configured operations of the table above, registration (ICP,
  Coherent Point Drift), point sampling, keypoint analysis, k-means and point-cloud
  consolidation. The remaining editor commands (mesh denoise, remesh, subdivide and simplify,
  scalar ridge, progressive Poisson, parameterization, scene save/load, visibility, camera
  controller) are owned by
  [RUNTIME-312](../../tasks/backlog/runtime/RUNTIME-312-agent-lane-mcp-hardening-and-coverage.md).
- Imports are asynchronous (`Pending`); poll `scene_entities` for the result.
- History entries that a queued editor job publishes on a later frame (every `Pending` run:
  registration, point analysis, normal estimation and the like) carry the command's own label
  without the `Agent: ` prefix, because the label scope covers only the call itself;
  synchronous operations are prefixed.
- Unix-domain sockets only; Windows builds report `Unsupported`.
- A Sandbox killed by a signal leaves its socket file; the next start replaces it.
