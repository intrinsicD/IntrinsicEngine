# Archived target documents

On 2026-10-30 the operator decided that IntrinsicEngine has **one** current architecture target,
[Engine and feature modules](../architecture/engine-and-feature-modules.md), replaced only when its
goals are met, and that all earlier target, vision and plan documents are archived (operator: "Also
ich hätte gerne alle alten zieldokumente archiviert. Dann nehmen wir nur dieses hier als aktuelles
und das wird erst ersetzt wenn die Ziele erfüllt sind.").

The files below are kept for history. They are not design guidance; where one conflicts with the
current target, the target wins. Accepted ADRs stay in [`docs/adr/`](../adr/index.md) as decision
records; ADR 0031 is marked superseded.

| document | title |
|---|---|
| [`architecture/declared-operations-table.md`](architecture/declared-operations-table.md) | Named operations for ADR 0031 (projection basis) |
| [`architecture/feature-module-playbook.md`](architecture/feature-module-playbook.md) | Feature Module Playbook (Refactor + New Development) |
| [`architecture/geometry-pipeline-modularity.md`](architecture/geometry-pipeline-modularity.md) | Geometry Processing Pipeline Modularity |
| [`architecture/gpu-driven-modular-rendering-pipeline-plan.md`](architecture/gpu-driven-modular-rendering-pipeline-plan.md) | Modular GPU-Driven Rendering Pipeline Plan (Code-Aware Reuse + Gap Audit) |
| [`architecture/ground-up-redesign-blueprint-2026.md`](architecture/ground-up-redesign-blueprint-2026.md) | Ground-Up Redesign Blueprint (2026 End-State Reference) |
| [`architecture/ground-up-redesign-vision.md`](architecture/ground-up-redesign-vision.md) | IntrinsicEngine: Ground-Up Redesign Vision and Priority Stack |
| [`architecture/kernel-target-state.md`](architecture/kernel-target-state.md) | Kernel target state — the north star for runtime work |
| [`architecture/moonshot.md`](architecture/moonshot.md) | MOONSHOT — IntrinsicEngine as a living geometry laboratory |
| [`architecture/rendering-target-architecture.md`](architecture/rendering-target-architecture.md) | IntrinsicEngine — Graphics Rendering Target Architecture |
| [`architecture/retained-geometry-lifecycle-consolidation-plan.md`](architecture/retained-geometry-lifecycle-consolidation-plan.md) | Retained Geometry Lifecycle Consolidation Audit |
| [`architecture/runtime-subsystem-boundaries.md`](architecture/runtime-subsystem-boundaries.md) | Runtime Subsystem Boundaries & Lifecycle Map |
| [`architecture/verification-evidence-architecture.md`](architecture/verification-evidence-architecture.md) | Verification evidence architecture (planned) |
| [`migration/target-repo-layout.md`](migration/target-repo-layout.md) | Target Repository Layout (Migration Contract) |
| [`architecture/roadmap-architecture-sequencing.md`](architecture/roadmap-architecture-sequencing.md) | the near-term architecture sequencing section of `docs/roadmap.md` |

Four of them keep a short forwarding page at their old path under `docs/architecture/`, because
frozen task records link there: `geometry-pipeline-modularity.md`, `kernel-target-state.md`,
`rendering-target-architecture.md`, `verification-evidence-architecture.md`.
