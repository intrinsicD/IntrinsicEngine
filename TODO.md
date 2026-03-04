# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks **what's left to do** in IntrinsicEngine's architecture.

**Policy:** If something is fixed/refactored, it should **not** remain as an "issue" here. We rely on Git history for the past.

---

## 0. Scope & Success Criteria

**Goal:** a data-oriented, testable, modular engine architecture with:

- Deterministic per-frame orchestration (CPU + GPU), explicit dependencies.
- Robust multithreading contracts in Core/RHI.
- Minimal "god objects"; subsystems testable in isolation.

---

## 1. Related Documents

- `docs/architecture/rendering-three-pass.md` — canonical three-pass rendering architecture spec (pass contracts, data contracts, invariants).
- `PLAN.md` — archival index for the completed rendering refactor.
- `ROADMAP.md` — feature roadmap, prioritization phases, long-horizon planning, rendering modality redesign vision, and architecture SLOs.
- `README.md` — architecture SLOs section with measurable SLO thresholds and telemetry milestones.
- `CLAUDE.md` — development conventions, C++23 adoption policy, architectural invariants.

---

## 2. Near-Term Epics (from `ROADMAP.md`)

This is the active dependency-ordered execution queue. Complete top-to-bottom unless sequencing rules explicitly allow overlap.

### Epic 1 — HDR Post-Processing Foundation (P0)

**Why now:** Most rendering features in `ROADMAP.md` depend on an HDR intermediate target and post chain.

**Scope (MVP):**
- Add HDR scene color target (`R16G16B16A16_SFLOAT`) and move forward shading output off swapchain.
- Add `RenderGraph` post chain with at least:
  - `Post.ToneMap` (ACES + Reinhard selectable).
  - `Post.FXAA` (toggleable pass).
- Present path consumes only LDR post output.
- Add runtime pass toggles in editor viewport panel.

**Implementation tasks:**
- Introduce explicit pass resources (`HdrColor`, `LdrColor`, sampled depth when needed).
- Add post constants UBO/push constants (exposure, tone-operator enum, FXAA params).
- Ensure pass order and barriers are encoded in render graph (no implicit sync).
- Add shader permutation registration and hot-reload plumbing for post shaders.

**Acceptance criteria:**
- Forward pass no longer writes directly to swapchain.
- Disabling post chain still routes through HDR->LDR conversion (identity-safe path).
- Visual parity at default settings, with measurable anti-aliasing improvement on thin wireframe edges.
- Automated test coverage: pass registration + graph compile smoke test.

---

### Epic 2 — Scene Serialization MVP (P1)

**Why now:** Unlocks practical workflows and test reproducibility for all subsequent editor features.

**Scope (MVP):**
- Deterministic scene save/load for entity hierarchy + core component payloads + asset references.
- Project file references scene path and editor layout preset.

**Implementation tasks:**
- Define stable scene schema (`version`, `entities`, `components`, `assetRefs`).
- Add serializer/deserializer in `Runtime` with explicit component adapters.
- Implement asset-handle remap/validation on load with structured diagnostics.
- Add "Save Scene", "Load Scene", and dirty-state prompt wiring in editor UI.

**Acceptance criteria:**
- Save->load->save round-trip is byte-stable (or canonicalized-text stable) for unchanged scenes.
- Missing assets do not crash load; failures are surfaced in UI/log with entity/component context.
- Integration test verifies hierarchy/component parity after reload.

---

### Epic 3 — Transform Gizmos + Viewport Toolbar Baseline (P1)

**Why now:** Immediate usability multiplier for all geometry and rendering workflows.

**Scope (MVP):**
- Translate/rotate/scale gizmos rendered via `LinePass` transient path.
- Viewport toolbar for mode switching + snap toggles + world/local orientation.

**Implementation tasks:**
- Implement interaction state machine (idle/hover/active) with deterministic picking priority.
- Add local/world transform basis support and configurable snap increments.
- Add multi-selection pivot strategy (`Centroid`, `FirstSelected`).
- Bind toolbar controls to existing inspector selection model.

**Acceptance criteria:**
- Dragging gizmo updates transform in real time without inspector-only edits.
- Snap and orientation toggles affect manipulation immediately and predictably.
- Multi-object transform applies from selected pivot mode with no entity drift.
- UI smoke tests cover toolbar toggle persistence.

---

### Epic 4 — Profiling & Benchmark Harness (P2)

**Why now:** Establishes objective regression guardrails before expanding expensive render features.

**Scope (MVP):**
- GPU timestamp queries per major render pass.
- CPU per-system frame timings exposed in telemetry panel.
- Deterministic benchmark runner for fixed scene/camera trajectories with JSON output.

**Implementation tasks:**
- Add timestamp write/resolve around pass boundaries and map to pass IDs.
- Extend telemetry schema for min/avg/max/p95/p99 frame and pass timings.
- Add headless benchmark mode (`--benchmark <scene> --frames N --out file.json`).
- Add threshold-based regression check script for CI/local use.

**Acceptance criteria:**
- Performance panel shows per-pass GPU timings and per-system CPU timings in one frame timeline.
- Benchmark runs are reproducible (same scene seed/path => low variance envelope).
- Regression gate fails when threshold is exceeded and prints offending metrics.

---

### Sequencing Rules

- Start Epic 2 only after Epic 1 HDR/LDR route is merged (render output contract stability).
- Epic 3 can overlap late Epic 2, but selection/picking changes must not block scene I/O.
- Epic 4 instrumentation starts once Epic 1 is in place so timing baselines include post chain.

### Cross-Epic Definition of Done

- Update `README.md` for each merged epic (usage + architecture deltas).
- Remove superseded code paths immediately (no compatibility clutter beyond staged migration windows).
- Add at least one integration test per epic and wire into existing test targets.
