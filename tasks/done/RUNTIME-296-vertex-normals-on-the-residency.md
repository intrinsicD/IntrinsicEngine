---
id: RUNTIME-296
theme: I
depends_on: [RUNTIME-292, GRAPHICS-155]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-296 — Vertex normals on the GPU property residency

## Goal
- RUNTIME-294 row "Vertex normals (face-weighted, face normals)": today CPU only.
- Input: positions from the canonical residency slot (`ResolveGpuPropertyInput`); the face rings as a vertex->face CSR bundle uploaded once per topology revision through the residency (derived key, revision = the topology watches' signature) and reported in bytes.
- Kernels: a face-normal pass, then a per-vertex gather over the CSR in the CPU reference's face order (deterministic, no float atomics; double precision like the reference).
- Output: a typed vec3 normal ring; Accept through a transaction mirroring the scalar precedent (readback, the existing undoable normals publication, `BindRevision`); batch and agent auto-accept.
- Backend option on the normals config (`vulkan`), panel Accept / Discard.
- Preview: no. Vec3 normal rings are not observed (the observer covers positions and colormap scalars); the render block keeps its CPU normals until Accept.
- CPU stage: none left for the face-weighted method (everything ported). Face normals (`mesh_face_normals`, Newell area vectors on the face domain) stay CPU in the first slice and follow in a second slice of this task.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: named mesh vertex positions plus polygon face rings and halfedge topology (`f:halfedge`, `h:next`, `h:to_vertex`, `h:face`, deletion masks). |
| Compatible entity sources | Mesh vertices (face-weighted) and mesh faces (face normals); positions come from the residency slot, never a render buffer. |
| RuntimeModule | `Extrinsic.Runtime.NormalOperations` (existing); the device kernels in a `graphics/renderer` workspace. |
| Config/agent | `sandbox.normal_estimation` gains backend `vulkan` for `mesh_face_weighted` and `mesh_face_normals`; the result and agent output carry the residency IO counters (input uploads, topology bytes, reuse). |
| UI | Normal Estimation window: Backend combo for both mesh methods; Accept / Discard of the pending GPU result. |
| Publication | Vec3 output on the vertex domain (face-weighted) or the face domain (face normals), every row (deleted rows keep their bytes). GPU preview: no; commit via the normals transaction (Accept -> `Estimate normals` history entry -> `BindRevision`). |
| End-to-end tests | `NormalTransaction.*` contract tests, the panel test, gpu;vulkan `RUNTIME296VertexNormalsResidency.*` (parity vs the CPU reference, zero-upload second run, Accept + undo). |

## Completion — 2026-09-30
Commit: `e8364ccf2` (slice 1) and `a61a906fe` (slice 2) on `claude/cpd-nystrom` (see RETIREMENT-LOG). The
slice 2 commit retired the task. Both mesh normal methods run on the GPU property residency with backend
`vulkan`:
- `mesh_face_weighted`: fp64 face pass plus a deterministic per-vertex gather over a
  vertex->face CSR in the reference's order (uniform, area, max; the angle weightings stay on
  the CPU).
- `mesh_face_normals`: one Newell pass per processed face.
- Positions come from the canonical slot. The topology bundles (`#vertex_normal_topology`,
  `#face_normal_topology`) are resident per topology revision. Output is a typed vec3 ring;
  Accept goes through the undoable normals publication and then `BindRevision`. Batch and agent
  runs auto-accept.
- Dispatches are chunked, and size limits and subnormal inputs are refused at admission. There
  is no viewport preview, because vec3 rings are not observed.

Evidence:
- Contract tests `NormalTransaction.*` (9), `VertexNormalsWorkspace.*`, the corner-walk
  precedence unit test and the panel tests (Accept/Discard, detach discards).
- gpu;vulkan `RUNTIME296VertexNormalsResidency.*`:
  - measured max delta 0 against the CPU reference for area, max, uniform and face normals;
  - second runs upload 0 bytes;
  - Discard keeps the rows; undo restores them.
- Slice 1 was implemented by Fable 5.1 and reviewed by Codex 6 Astra (7 findings fixed). Slice 2
  was completed by Claude Opus 5.5 from Fable's partial draft and reviewed clean by Codex.
- CPU gate 5334/5334. GPU suite 130/132: 1 opt-in skip and the environmental
  `VulkanShutdownLsanContract` failure. Operational.

## Acceptance criteria
- [x] Contract tests (mock device): Accept publishes undoably and binds the front; Discard; stale input/output; the CSR bundle is resident per topology revision (second resolve uploads nothing, a topology edit uploads once).
- [x] gpu;vulkan parity smoke: the accepted rows equal the CPU reference within a stated, justified tolerance.
- [x] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [x] Panel Accept / Discard (Accept disabled with its reason when stale); batch and agent commands accept automatically.
- [x] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.
- [x] Face normals (`mesh_face_normals`) run on the residency too (second slice).

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```

## Log
- 2026-09-30 (slice 1, Fable 5.1): `mesh_face_weighted` on the residency. Decisions taken while
  implementing:
  - The kernels run in double precision with `precise` arithmetic in the reference's summation
    order (fan from the first corner; faces in ascending index order per vertex), so parity is
    float-ulp level (smoke bound 2e-6 per component, the max delta recorded as a test property).
  - Uniform, area and max weighting run on the device; the angle weightings need a double
    `acos` (not in GLSL) and are refused for `vulkan` (CPU only) rather than computed in float.
  - The topology bundle is resident under a derived key (`#vertex_normal_topology`), revision =
    hash of the topology and deletion watches; its fixed section order lets a resident bundle
    describe its layout from the source counts, so a hit reconstructs no halfedge mesh.
  - GPU preview: no. Vec3 rings are neither colormap scalars nor positions; the observer does
    not bind them. A normal-channel preview (a GpuWorld copy like the position preview) is not
    part of this task.
  - `EditorGpuTransactionPhase` moved to `Runtime.EditorCommon` and is shared with property
    smoothing (was `EditorPropertySmoothingPhase`).
  - `mesh_face_normals` (Newell area vectors, face domain) stays CPU: second slice.
- 2026-09-30 (review fixes, Codex 6 Astra): dispatches are chunked with a base index within the
  guaranteed 65535 workgroups; size limits (2^24 faces / vertices, 2^26 corners) are checked
  before allocation in the graphics layer and at admission; subnormal coordinates are refused
  for `vulkan` (no dependence on float32 denorm preservation); detaching the panels discards a
  pending normals or smoothing transaction; the reference's per-corner diagnostic precedence
  (non-finite before a later invalid corner) is restored and pinned; the smoke asserts finite
  operands and compares uniform weighting too.
- 2026-09-30 (slice 2, Claude Opus 5.5 after Fable's partial draft): `mesh_face_normals` on the
  residency. One pass per processed face (Newell area vector in double, ring order, deleted
  corners as a sentinel -> fallback) into the face ring; the face bundle carries rings only.
  Contract tests `NormalTransaction.FaceNormals*`; the smoke's face runs measure delta 0 and a
  zero-upload second run. Panel Backend combo for face normals.
