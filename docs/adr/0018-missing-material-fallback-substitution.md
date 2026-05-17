# ADR 0018 — Missing-Material Fallback Substitution and Diagnostics

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics (renderer span-copy substitution step, `MaterialSystemDiagnostics` counters)
- **Related tasks:** [`tasks/done/GRAPHICS-031`](../../tasks/done/GRAPHICS-031-default-debug-surface-material.md) (parent planning), [`GRAPHICS-031B`](../../tasks/done/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md) (substitution + diagnostics implementation)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `## Material registry and slot contract` GRAPHICS-031 paragraph (lines 747–764) in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/active/DOCS-001-reduce-graphics-architecture-prose.md). Kept distinct from [ADR-0017](0017-default-debug-surface-material.md) because the substitution policy (when / where slot 0 replaces a snapshot record's resolved slot, which counters increment, and that there are no silent-skip paths) is a separately traceable decision from the material definition itself (slot 0 registration, shader pair, pipeline state); per the [`DOCS-001`](../../tasks/active/DOCS-001-reduce-graphics-architecture-prose.md) `## Forbidden changes` rule, multiple decisions must not be folded into a single ADR.
- **Related ADRs:** [ADR-0017](0017-default-debug-surface-material.md) records the slot-0 material that this ADR substitutes in.

## Context

[ADR-0017](0017-default-debug-surface-material.md) records **what slot 0 contains** (registration name, shader pair, pipeline state, cull-bucket reuse). This ADR records **when and where slot 0 is substituted in** for a snapshot record whose resolved material slot is unset or out-of-range, and **which counters** track that substitution.

Three forces shape the decision:

1. The substitution must be **graphics-owned at snapshot consumption time**, not runtime-side. Runtime authors `SubmitRuntimeSnapshots()` records without needing to know the graphics-side slot-0 identity; if runtime had to know `kDefaultMaterialSlotIndex`, every editor / extraction site that authors a missing material would have to import a graphics constant.
2. The substitution must use a **separately-named counter set** so editors / runtime tools can distinguish:
   - "Authoring left the material sentinel-unset" (the editor never wrote a material).
   - "Authoring wrote a stale or out-of-range slot integer" (the editor wrote a value that referenced a since-freed slot).
   - "Slot 0 was used this frame" (cumulative per-frame usage, including both substitution outcomes plus any explicit slot-0 requests).
3. There must be **no silent-skip paths** — every missing-material case must increment a counter and substitute, so diagnostics surface what is actually happening on a per-frame basis. The stale-handle resolution path inside `GetMaterialSlot()` already has its own counter (`FallbackSlotResolveCount`); this ADR's counters are additive to it, not a replacement.

This ADR captures the substitution location, counter set, runtime-agnostic property, and the "no silent skip" rule. `docs/architecture/graphics.md` keeps a short canonical pointer to this ADR.

## Decision

### 1. Substitution is graphics-owned at snapshot consumption time

The missing-material fallback policy is **graphics-owned at snapshot consumption time**:

- When the renderer copies a runtime-submitted snapshot record whose resolved material slot is **unset** or **out-of-range**, it substitutes `kDefaultMaterialSlotIndex`.
- The substitution lives at the same renderer span-copy step that already drains `InvalidSnapshotRecordCount`.
- Runtime stays **agnostic** of graphics-side slot identity. Runtime never imports `kDefaultMaterialSlotIndex` and never substitutes on its own.

### 2. Three additive `MaterialSystemDiagnostics` counters

The substitution increments one of three additive counters on `MaterialSystemDiagnostics`:

- **`MissingMaterialFallbackCount`** — sentinel-unset authoring. The runtime-submitted record carried a sentinel "unset material" description (or equivalent default-constructed material slot).
- **`InvalidMaterialSlotCount`** — out-of-range / stale slot integers. The runtime-submitted record carried a numeric slot value that resolved past the current material slot count or to a since-freed slot.
- **`DefaultDebugSurfaceUses`** — total per-frame uses of slot 0 after substitution. This counts every record that lands on slot 0, including both substitution outcomes above **and** any explicit slot-0 requests from runtime authoring.

The three counters are **additive**, not exclusive: a single substitution increments `DefaultDebugSurfaceUses` plus exactly one of `MissingMaterialFallbackCount` / `InvalidMaterialSlotCount`. An explicit slot-0 request from runtime increments only `DefaultDebugSurfaceUses`.

### 3. Runtime-agnostic; explicit-default authoring is identical

Runtime authoring may also request the default explicitly through a sentinel "unset material" description. The renderer-side substitution and counter increments are **identical** in that case:

- `MissingMaterialFallbackCount` increments.
- `DefaultDebugSurfaceUses` increments.

Runtime does not need a separate "request default" code path that bypasses the substitution; the sentinel description is the seam.

### 4. No silent-skip paths

There are **no silent-skip fallback paths**:

- Every missing-material case must substitute and count.
- Every invalid-slot case must substitute and count.
- A future code path that "helpfully" skips a record because its material is missing would silently regress the diagnostics surface; reviewers must reject such a change.

### 5. `FallbackSlotResolveCount` remains separate

`MaterialSystemDiagnostics::FallbackSlotResolveCount` continues to track the **separate** stale-handle resolution path inside `GetMaterialSlot()`. That counter measures fallback resolution for handle-keyed lookups (a different code path from the snapshot span-copy step in §1).

The three counters added by this ADR (`MissingMaterialFallbackCount`, `InvalidMaterialSlotCount`, `DefaultDebugSurfaceUses`) are **additive** to `FallbackSlotResolveCount`, not replacements. Each counter has a single trigger site:

| Counter                          | Triggers at                                            | Cause                                           |
|----------------------------------|--------------------------------------------------------|-------------------------------------------------|
| `FallbackSlotResolveCount`       | Inside `GetMaterialSlot()` for handle-keyed lookups    | Stale or invalid handle resolution              |
| `MissingMaterialFallbackCount`   | Renderer span-copy step                                | Sentinel-unset material on snapshot record      |
| `InvalidMaterialSlotCount`       | Renderer span-copy step                                | Out-of-range / stale slot integer on record     |
| `DefaultDebugSurfaceUses`        | Renderer span-copy step (every slot-0 use)             | Any record that lands on slot 0                 |

## Consequences

Positive:

- Runtime stays agnostic of graphics-side slot identity; no import of `kDefaultMaterialSlotIndex` outside graphics.
- The three additive counters give editors a clean diagnostic split: "you forgot to assign a material" (`MissingMaterialFallbackCount`), "you assigned a stale slot" (`InvalidMaterialSlotCount`), and total slot-0 usage (`DefaultDebugSurfaceUses`).
- The substitution lives at the same renderer span-copy step that already drains `InvalidSnapshotRecordCount`, so reviewers have one location to validate per-record drops / substitutions.
- "No silent skip" means a frame's `DefaultDebugSurfaceUses` matches exactly what the developer sees on screen; an unexpected slot-0 use never disappears silently.
- `FallbackSlotResolveCount` and the three new counters are independent and measure different code paths; consumers cannot confuse handle-keyed fallbacks with snapshot-span-copy substitutions.

Trade-offs and risks:

- The substitution policy is in **renderer code**, not in a runtime adapter. A backend that bypassed the snapshot span-copy step (for example by recording draws directly from a backend-private mirror) would skip the substitution and the counters. Reviewers must enforce that all backends thread snapshot records through the canonical span-copy step.
- `DefaultDebugSurfaceUses` is cumulative per frame and includes explicit slot-0 requests. Consumers that want only the substitution count must compute `MissingMaterialFallbackCount + InvalidMaterialSlotCount`; the deliberate overlap surface explicit-slot-0 traffic too.
- The "no silent skip" rule means a buggy editor that floods the renderer with missing-material records will flood the counters every frame. The counters are the visibility seam; reviewers must reject any "rate limit" of the counters that would hide the flood.
- The separation from `FallbackSlotResolveCount` requires reviewers to know which path triggered which counter. The §5 table is the single seam for that mapping; a code change that adds a fourth counter site without updating the table would silently confuse consumers.
- Editor / runtime tools must distinguish "user explicitly wants the default debug surface" from "user accidentally left it unset". Both go through the sentinel description and both increment `MissingMaterialFallbackCount`; tools that want to distinguish them must do so above the snapshot layer.

Follow-up tasks required: none from this ADR. Any future per-record reason-tag growth (for example a "stale generation" vs "out-of-range" split on `InvalidMaterialSlotCount`) is a separate task; this ADR's three-counter shape is the canonical seam.

## Alternatives Considered

- **Runtime-side substitution.** Rejected per §1: every editor / extraction site would have to import `kDefaultMaterialSlotIndex` and reproduce the substitution; the snapshot span-copy step is the single seam.
- **One mega-counter (`SlotZeroSubstitutionCount`).** Rejected per §2: editors lose the diagnostic split between sentinel-unset, out-of-range, and explicit-slot-0; the three additive counters carry strictly more information.
- **Silent-skip records whose material is missing.** Rejected per §4: hides flood from a buggy editor and produces a missing-from-screen behavior that contradicts what diagnostics report.
- **Reuse `FallbackSlotResolveCount` for both code paths.** Rejected per §5: would merge handle-keyed fallbacks (inside `GetMaterialSlot()`) with snapshot-span-copy substitutions, breaking consumer ability to localize where a substitution actually originated.
- **Fold the substitution into [ADR-0017](0017-default-debug-surface-material.md).** Rejected per the [`DOCS-001`](../../tasks/active/DOCS-001-reduce-graphics-architecture-prose.md) `## Forbidden changes` rule: the substitution decision (when / where / which counters) is independently traceable from the material definition (slot 0 contents); one decision per ADR.
- **Reason-tagged `InvalidMaterialSlotCount` (separate counters for stale vs out-of-range).** Rejected as out of scope: a future per-reason split is a separate task and would extend the three-counter shape additively without rewriting it.

## Validation

- [`tasks/done/GRAPHICS-031`](../../tasks/done/GRAPHICS-031-default-debug-surface-material.md) records the parent planning contract for both [ADR-0017](0017-default-debug-surface-material.md) and this ADR.
- [`tasks/done/GRAPHICS-031B`](../../tasks/done/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md) records the substitution + diagnostics implementation captured in §§1–4.
- `src/graphics/renderer/README.md` carries the matching `MaterialSystem` substitution behavior bullet, including the three-counter shape.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises the snapshot span-copy substitution, the three counter sites, and the runtime-agnostic property (the same sentinel description triggers identical substitution + counter behavior) without requiring a Vulkan device.
