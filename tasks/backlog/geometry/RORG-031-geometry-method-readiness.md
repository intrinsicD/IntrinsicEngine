---
id: RORG-031E
theme: F
depends_on: []
---
# RORG-031E — Geometry and method-readiness backlog seed

## Goal
- Serve as the umbrella for geometry algorithm hardening and method-readiness
  work: keep the open-children inventory below honest, keep the
  geometry-to-methods dependency edges visible, and retire once every child is
  independently tracked and the seed adds no coordination value (the
  `RORG-031C` runtime-seed retirement pattern).

## Non-goals
- Implementing new geometry algorithms in this task.
- Changing geometry ownership boundaries in this task.
- Duplicating the per-task detail that lives in the child task files and
  [`tasks/backlog/geometry/README.md`](README.md).

## Context
- Geometry remains a core subsystem target for migration and future
  methods/paper integration; `geometry -> core` only.
- Open children (all under `tasks/backlog/geometry/`): `GEOM-013` (dual
  contouring), `GEOM-014` (FA-QEM simplification), `GEOM-016` (point-cloud
  filtering/density), `GEOM-017` (descriptors/registration seams), `GEOM-018`
  (parameterization diagnostics), `GEOM-019` (harmonic/Tutte, gated on
  GEOM-018 + GEOM-020), `GEOM-020` (sparse LDLT/LLT seam), `GEOM-023`
  (non-symmetric BiCGSTAB seam), `GEOM-024` (generalized eigensolver seam,
  gated on GEOM-020), `GEOM-026` (cross-domain vertex normal recomputation,
  gates `UI-022`).
- Retired children/foundations: `GEOM-005..012`, `GEOM-015`, `GEOM-021`,
  `GEOM-022`, `GEOIO-002` (narratives in the retirement log; index in the
  category README).
- Method-readiness edges this seed watches: `GEOM-020 → METHOD-002`,
  `GEOM-023 → METHOD-003`, `GEOM-024 → METHOD-006` (encoded in those tasks'
  `depends_on`), and `GEOM-017 →` future robust/global registration method
  packages.

## Required changes
- [ ] Keep the open-children list in Context aligned with the actual
      `tasks/backlog/geometry/` contents whenever a child opens or retires.
- [ ] Keep the method-readiness dependency edges current as method tasks open,
      re-gate, or retire.
- [ ] Retire this seed (with the closure note below) once the children are
      independently tracked and no cross-child coordination remains.

## Tests
- [ ] No code in this planning seed; the Verification block proves the
      inventory matches the tree.

## Docs
- [ ] Keep [`tasks/backlog/geometry/README.md`](README.md) and this seed
      consistent with each other and with `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] The open-children inventory in Context matches the files under
      `tasks/backlog/geometry/`.
- [ ] Every geometry→method gate this seed names is encoded in the consuming
      task's `depends_on` front-matter.
- [ ] Retirement happens only via the closure rule in Required changes.

## Verification
```bash
for t in GEOM-013 GEOM-014 GEOM-016 GEOM-017 GEOM-018 GEOM-019 GEOM-020 GEOM-023 GEOM-024 GEOM-026; do
  ls tasks/backlog/geometry/${t}-*.md >/dev/null || { echo "missing ${t}"; exit 1; }
done
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Implementing child-task scope under cover of this seed.

## Maturity
- Target: `Scaffolded` — this is a planning umbrella, and the scaffold is the
  intended endpoint; no `Operational` follow-up is owed. Each child closes at
  `CPUContracted` or higher on its own.
