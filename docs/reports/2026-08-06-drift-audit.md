# Repo-state drift audit — 2026-08-06

This fresh whole-tree audit follows
[`docs/agent/drift-audit-checklist.md`](../agent/drift-audit-checklist.md) and is
one of the commit-scoped gates for
[`REVIEW-003`](../../tasks/done/REVIEW-003-architecture-stability-right-sizing-readiness-audit.md).
It does not reuse the rejected REVIEW-003 baseline or its partial evidence.

## Window

- State audited: clean `main` commit
  `51e7faddad943ab7727e407d008e474ec076566d`, captured on 2026-08-06 at
  20:50 CEST.
- Scope: the whole current tree, including `src/`, `docs/`, `tasks/`, `tools/`,
  generated inventories, and the layering allowlist. The source-marker probe
  excludes `src/legacy/` as prescribed by the checklist; that tree no longer
  exists on the audited commit.
- Toolchain: Linux 6.14 x86_64, CMake 3.28.3, Clang/clang++/clang-scan-deps
  23.0.0, fresh `ci` preset.
- Durable command receipts:
  [`tasks/evidence/REVIEW-003/commands/`](../../tasks/evidence/REVIEW-003/commands/).

## Findings

| Row | Drift category | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | Generated-inventory drift | pass | `generate_module_inventory.py --root src` wrote a temporary inventory and `diff -u` matched `docs/api/generated/module_inventory.md` exactly. |
| 2 | Layering-allowlist exception drift | pass | `check_layering_allowlist_quality.py --root . --strict` reports zero entries and zero findings; the retired-owner cross-check prints nothing. |
| 3 | Active-task branch drift | pass | `tasks/active/` contains only REVIEW-003. Its front matter names the live `main` branch and this worktree, and its Git-common-dir claim is current. |
| 4 | Stale `(planned)` markers | pass | Fourteen matches were inspected. Checklist/contract/prior-report matches discuss the marker itself. The sole current architecture marker is [`verification-evidence-architecture.md`](../architecture/verification-evidence-architecture.md), whose title says `(planned)`, whose status is `roadmap`, and whose implementation is still owned by open [`CI-012`](../../tasks/backlog/process/CI-012-versioned-verification-evidence-graph.md). No marker names a landed feature. |
| 5 | Aspirational claim without marker | pass | The strict docs-sync check is clean. Present-tense claims for `SceneDocumentModule`, `RenderSubsystemRegistry`, `BorrowMeshAsGraphReadOnly`, and typed `FramePassId`/`RenderCommandRouter` routing resolve to production symbols and contract/integration tests. Sampling-based. |
| 6 | Dead public seam | pass | REVIEW-003 performed a complete inventory of the 35 exported `I*` and public role-named surfaces, rather than a small sample. Every item has a production consumer/test route or passes a named right-sizing keep condition; no public `*Bridge` remains. The full deletion-test table is in the [readiness report](2026-08-06-architecture-stability-readiness.md#right-sizing-inventory). |
| 7 | Untracked TODO/shim drift | pass | No `TODO`, `FIXME`, `XXX`, or `HACK` remains under promoted `src/`. Other matches are technical temporaries such as scratch/staging storage. `PresentPass` is described as an explicit finalization shim, not a migration bridge: it is a permanent typed recipe role with production and contract-test consumers. |
| 8 | Naming inconsistency | pass | Samples for `RenderGraph`, frame graph/framegraph layer vocabulary, and divergent underscore/hyphen variants are consistent. Apparent noncanonical strings occur only as examples in audit/checklist prose. Sampling-based. |
| 9 | Cross-doc reference rot | pass | `check_doc_links.py --root .` checked 3,179 relative links with no broken link. Sampled anchors for ECS element domains, geometry predicates/intersections, benchmark CI policy, and fast touched-scope verification resolve to current headers. Sampling-based. |

## Follow-ups

No findings and no follow-up tasks. A separate independent read-only pass also
found no unresolved dead or ceremonial public seam on the audited source
surface.

## Elapsed time

Approximately 18 minutes (20:50–21:08 CEST), including the complete
right-sizing inventory and the sampled manual rows. This is within the
checklist's 45-minute budget. Build and full-CPU execution are recorded by the
parent readiness gate, not charged to this drift-only timing.

## Cadence note

This on-demand report establishes the REVIEW-003 baseline. It remains a
commit-scoped observation, not a permanent guarantee or a new CI gate.
