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
(`AgentOperationSpec::NeedsPresentedFrame`; any new tool that may dispatch GPU work sets it).
`jobs_wait` sets it too, so no wait outlives a minimize (the job it watches may be GPU work). A call of those
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
  until the reply. The source is the call's own run, read through the shared operation-progress
  model (`EditorJobCommandSurface::Progress`, UI-069): a deferred operation captures its run key
  where it is known and returns it as `AgentOperationOutcome::Progress`. Editor-job commands
  (`FinishApply`) key the job they queued (the new job since the command ran); K-Means and
  consolidation (`AwaitServiceRun`) key the correlation id their submission returned; scene save and
  load key their job token (projected from the job service, since the session does not index it). A determinate
  run reports `progress` = percent with `total` 100; otherwise `progress` is the run's elapsed
  seconds without `total`; `message` is the run's label (the job's debug name). A call with no run
  key (a capture, or an `import_file` wait), or whose job is not queued yet, reports its age in seconds with message
  `waiting`; no other job ever stands in for it. MCP requires `progress` to strictly increase, so
  a call keeps the unit chosen at its first notification (percent with `total`, or seconds without
  `total`; percent never exceeds 100) and a notification is skipped unless its value is larger than
  the previous one. The panels draw the same model with the shared widget, so an agent run shows in
  the Sandbox panels too.
- Cancellation: `notifications/cancelled {requestId}` (JSON-typed comparison: `1` is not `"1"`)
  stops the reply and the progress of that call and runs its `AgentOperationOutcome::Cancel` hook
  (RUNTIME-279), whose result the server logs. An editor-job command (`FinishApply`) cancels, at cancel time and
  through `EditorJobCommandSurface::Cancel`, the active jobs of the runs it queued (RUNTIME-313):
  a run is named by its first job's token, which every later stage carries as
  `EditorJobIdentity::Run`, so stages queued after the call started (a GPU Accept queued when the
  compute stage publishes) are reached too and another run on the same output never is
  (`CancelEditorRuns`, which visits each run once by its head). Once the call's result arrived the hook cancels nothing. Only jobs the editor
  submitted are ever touched. The job ends `Cancelled` on a later drain without publishing
  anything (no property, no history entry) and its unpublished finalizer runs exactly once: it
  delivers the command's terminal failure, or (where a finalizer only abandons its run) releases
  the callback, which ends the call as `result_unavailable`. The entry stays as a tombstone that counts against the 16-call
  cap until its continuation completes (that delivery ends it), then it is dropped silently, so
  call-and-cancel cannot grow the queue. A cancelled `jobs_wait` ends at the next poll (its hook
  ends the wait; the job is not affected). Calls with no editor job keep running to their end:
  K-Means and consolidation runs (their jobs belong to the services, not the editor surface),
  scene save/load, imports and captures. A run whose job was cancelled some other way
  (`jobs_cancel`, or any other caller of the editor surface's `Cancel`; the panels do not cancel
  through it yet) answers with the error code `cancelled`. Only a requested cancel relabels a run,
  and only when the run ended not applied (`StaleEntity`): the call's own hook, or a cancel the
  surface accepted for one of the run's jobs, which it remembers per run past the jobs' reaping
  (`RunCancelRequested`; minimized frames drain and reap before the call's next poll). A stage
  cancelled because an earlier stage failed, or an older run's cancelled job on the same output,
  leaves the run's own failure as its answer. One or two
  progress notifications already queued may still arrive after the cancel (allowed by the
  specification).
- Tool results are the existing JSON text content plus, when the negotiated version is
  `2025-06-18` or newer, `structuredContent`: the JSON object the tool returned, or
  `{"error":{"code","message"}}` for errors that carry a machine-readable `ErrorCode`
  (for example `file_exists`). Older revisions get the text content only.
- There is no `outputSchema`: it is optional in the specification and the per-tool result
  shapes are still moving, so a schema now would be a promise the tools do not yet keep.
- Annotations: `readOnlyHint` follows `ReadOnly`. `destructiveHint` (`AgentOperationSpec::Destructive`)
  is true for a mutation the undo history cannot restore and false for everything that records an
  `Agent: ` history entry: today `view_capture` and `save_scene` (write files), `load_scene`
  (replaces the scene document) and `config_apply` (changes engine
  configuration, which is not in the history). Mutating tools that edit the scene or its
  properties (`import_file`, `show_property`, `run_*`) are undoable, `undo`/`redo` operate on
  the history itself, and `select_entity` and `set_camera` change editor state (selection, the camera
  controller and pose), not scene data, so they are deliberately not destructive. `jobs_cancel` is not
  destructive either: a cancelled job publishes nothing, so the scene, files and history stay as
  they were (the lost computation can be run again). New tools classify themselves with this rule.
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
  `config_schema`, `config_get`, `config_preview`, `history`, `jobs_list`, `jobs_wait`, `log`,
  `preview_registration`, `preview_point_sampling`, `preview_keypoint_analysis`, `preview_kmeans`,
  `preview_point_cloud_consolidation`, `preview_operation`, `preview_mesh_operation` and
  `view_screenshot`. State-changing: `select_entity`, `import_file`, `show_property`,
  `config_apply`, `save_scene`, `load_scene`, `set_visibility`, `set_camera`, `undo`, `redo`, `jobs_cancel`,
  `run_operation`,
  `run_mesh_operation`, `run_registration` (ICP
  or Coherent Point Drift from their config sections; the reply waits for the job),
  `run_point_sampling` (the `sandbox.point_sampling` section), `run_keypoint_analysis`,
  `run_kmeans` and `run_point_cloud_consolidation`. `view_capture` writes a PNG inside the
  allowed roots.
- Jobs (RUNTIME-279). `jobs_list` lists every job the job service retains with its token
  (`"<index>:<generation>"`), state, progress and elapsed time; a job the editor submitted through
  `EditorJobCommandSurface::Submit` also names its `editor` output (entity, output property) and
  `cancellable: true` while active. Those rows, and only those, are what `jobs_cancel {token}`
  accepts (it calls `EditorJobCommandSurface::Cancel`): asset decode and imports, scene files and
  K-Means and consolidation runs (service jobs found by correlation id) answer `not_editor_job`,
  an ended job `job_not_active`. A cancel on a queued or running job, or one parked for apply,
  never publishes: the job service checks the cancel flag before every apply, so a GPU result is
  discarded by its job's finalizer instead of half accepted. `jobs_wait {token | entity + output,
  timeout_ms ≤ 60000 (default 30000)}` is read-only and never blocks the main thread: it answers at
  once or returns a continuation the server polls each frame, and ends when the job completed
  (terminal and its result delivered: `finished: true`), at the deadline (`timed_out: true`), or
  with an error when the job is unknown or was reaped (`unknown_job`), the scene was replaced
  (`scene_replaced`, from the session scene epoch), the workspace detached or the window
  minimized (`viewport_not_presentable`). A job the wait has seen that ends and is reaped before the
  next poll (one with no result to deliver completes and is reaped in the same frame) answers
  `finished: true, reaped: true` with its last seen row, whose `state` is `ended` when that row was
  still running (the terminal state was not observed), or `scene_replaced` when the scene changed
  meanwhile; a job already reaped when the wait is
  read answers `unknown_job`. By entity and output it waits for the newest editor run
  writing that output when called; later runs are not followed. Several waits on one job are
  independent.
- Appearance and camera. `set_visibility` shows or hides a lane of an entity (`lane`: surface, edges or
  points, default the entity's primary one: mesh surface, graph edges, point-cloud points)
  through `ApplyEditorRenderHintCommand` exactly as the appearance panel's checkboxes do (one
  undoable step). The unified Appearance panel's per-attribute source selectors (Position, Normal, Texcoord,
  Color, Point size, Line width per element domain) call `ApplyEditorAttributeBindingCommand`; the agent
  mirror (`attribute_bindings`, `bind_attribute`) is owned by `RUNTIME-316`. `set_camera` takes exactly one of: `controller` (orbit, fly, free look, top down;
  `ApplyEditorCameraControllerCommand`, like the Camera panel's buttons), `pose` (`position`,
  `target`, optional `up`), `preset` (front, back, left, right, top, bottom, isometric; frames
  `entities`, or everything with world bounds) or `focus: true` (frames `entities`, or the
  selection). Pose, preset and focus go through `ApplyEditorCameraPoseCommand`, the command the
  Camera panel's View buttons and "Focus selection" use; presets share their axes and framing
  with `view_capture`. The reply carries the `previous` and `current` pose
  (`position`/`forward`/`up`) and two flags: `up_ignored` (fly and top-down have no roll, so `up`
  is a hint; free look derives its roll from it) and `position_clamped` (the orbit radius limit).
  A view the active controller cannot look along (top-down only looks along -Y; fly and free
  look stop 1 degree short of the poles) is refused as `UnsupportedCameraPose` with the camera
  untouched, as are non-finite or degenerate poses and unknown entities; a preset or focus with
  nothing bounded to frame is an error too, since the camera did not move. Camera changes are
  editor view state, not scene data, so they are neither undoable nor destructive. Orbit
  yaw/pitch/radius are not separate inputs: `position = target - direction * radius` expresses
  them.
- Scene files. `save_scene` writes the scene document to a path inside the allowed roots (it
  refuses an existing file unless `overwrite: true`, error code `file_exists`) and `load_scene`
  replaces the whole scene document with a file inside them; both resolve the path with
  `ResolveAgentPath`, answer once the job finished and are `Destructive` (a written file and a
  replaced document are outside the undo history). `save_scene` with `overwrite: true` replaces
  whatever file lies at the path inside the roots, of any type; the existence check and the
  write are two steps, so a file created in between is overwritten without the check (the lane
  has one client and one main thread, the path is not reserved, and a symlink planted by another local
  process between the check and the write is outside the threat model: the socket is owner-only). A path that passes through a
  dangling symlink, or equal to an allowed root itself, is outside the roots: `ResolveAgentPath` refuses it, so no file tool can write
  through a link to a target elsewhere. Only the latest scene-file event is kept, so a save or
  load that a later one overtook (or a stale job that publishes none) answers with the error code
  `result_unavailable` once its job ended. `import_file` with `wait: true` follows the import by
  its handle (also after "Clear completed" hid its queue row), answers when it completed,
  failed or was cancelled and reports `entities_created` from the import's own result;
  `new_entities` lists the entity ids that appeared meanwhile, which also includes entities of
  other imports running at the same time.
- Configured operations. `run_operation` / `preview_operation` select a row of one table by
  `operation`: property smoothing, spectral modes, harmonic field, scalar gradient, mesh
  curvature, geodesics, curvature segmentation, normal estimation, kernel density, point
  spacing, outlier analysis, density weight, descriptor analysis, bilateral filter, point
  construction, progressive Poisson, parameterization, scalar ridges and the mesh topology
  operations (denoise, remesh, subdivide, simplify). Each row calls its panel's
  `Preview*`/`Apply*` path with the settings of its config section (`config_apply` first). The
  mesh topology operations and scalar ridges have no section and take a `params` object. Their
  command owners declare every field once as a `ConfigFieldSpec` table (name, type, range, enum
  names, description: `EditorMeshDenoiseFieldSpecs()` and siblings, `EditorScalarRidgeFieldSpecs()`);
  the commands validate against it (so an agent cannot exceed the ranges the panel's controls
  allow, for example 10 subdivision iterations), the panels' controls read the same constants,
  and the tool schema is generated from it: `run_operation`/`preview_operation` carry one JSON
  Schema `if/then` per operation in `allOf` (type, range, enum names and the defaults of the
  command struct's own member initializers) and the description lists the same. Enums take their
  name or integer code. Bad params end with the error code `invalid_params`. `mesh_simplify` needs
  `target_faces` or `max_error` above 0 (its defaults alone do not run). The tool description lists
  every row with its section or params and whether the entity is an argument. A scalar ridge
  picks its `property` from the entity's vertex scalars like the panel's combo, publishes its
  curve graph as one undo step and, with `publish_mesh_features`, mesh feature properties as a
  second one (undo with `steps: 2`). Geodesics, curvature segmentation, scalar ridges and the
  topology operations report their panel's readiness; parameterization alone has no readiness
  function (its panel gates on the selection), so its preview answers `"enabled": null`. A
  `Pending` command answers when its job delivered (`result_unavailable` when none can). The
  tool flag `NeedsPresentedFrame` is per tool, so `run_operation` (as `run_mesh_operation`) is
  refused while minimized even for CPU-only rows. Behavior of the mesh aliases after the move
  into the table: a missing config section answers `enabled: false` with a reason (it was a call
  error), and a Vulkan property smoothing waits for its job (it was reported as an error while
  pending). Agent runs do not feed the panels' "last result" displays; the reply is the result.
  `run_mesh_operation` and `preview_mesh_operation` are aliases limited to the four mesh-field rows.
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
  undo history shows each agent change and the operator can undo it. A job queued by the call
  publishes on a later frame, so the submit paths (`CarryEditorLabelPrefix` in the workspace
  session's job surface, the k-means and consolidation requests) carry the prefix to that
  commit; a job a panel queues carries none.
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
jobs ([RUNTIME-279](../../tasks/done/RUNTIME-279-editor-job-snapshot-and-cancel.md),
[UI-060](../../tasks/done/UI-060-jobs-window.md)),
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
[RUNTIME-312](../../tasks/done/RUNTIME-312-agent-lane-mcp-hardening-and-coverage.md).

## Limitations

- Operation tools exist for the configured operations of the table above, registration (ICP,
  Coherent Point Drift), point sampling, keypoint analysis, k-means and point-cloud
  consolidation, scene files, visibility and the main camera (controller kind, pose, presets and
  focus). The camera is not undoable and `set_camera` pose and presets act on the `Main` slot only.
- Imports are asynchronous (`Pending`): pass `wait: true`, or poll `scene_entities` for the result.
- Unix-domain sockets only; Windows builds report `Unsupported`.
- A Sandbox killed by a signal leaves its socket file; the next start replaces it.
