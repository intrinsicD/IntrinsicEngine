# Clean-workshop review — ARCH-014 kernel convergence closure

## Change under review

- Change: retire [`RUNTIME-186`](../../tasks/done/RUNTIME-186-retire-engine-auxiliary-surface.md),
  [`RUNTIME-187`](../../tasks/done/RUNTIME-187-finalize-domain-free-engine-surface.md),
  and the [`ARCH-014`](../../tasks/done/ARCH-014-kernel-convergence-tracking.md)
  umbrella after settling the Engine API, moving all private state behind
  `Engine::Impl`, and closing the exact convergence policy at `12/0/0/5`.
- Trigger(s): changes the runtime public/import boundary and composition-root
  storage representation, then closes an architecture-convergence program.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` and `check_layering.py --root src --strict` scanned 753 files and 6,767 references with zero violations. Engine remains the runtime composition root and lower layers gained no runtime dependency. |
| 2 | CMake target links match layer policy | pass | No CMake file, target, or link edge changed. Implementation-only imports remain inside the existing runtime target. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The public Engine surface shrank: two re-exports and five auxiliary forwarders were removed. Its twelve exact imports are kernel substrate required by retained declarations; the five getter return/owning types are ratcheted explicitly. No lower-layer public surface changed. |
| 4 | Renderer member/subsystem growth justified by an owning seam | pass | No renderer state or subsystem was added. Existing Engine-owned kernel state moved mechanically into one opaque `Engine::Impl`; no domain owner was hidden there. |
| 5 | New passes use typed IDs, not string routing | n/a | No renderer pass, recipe pass, command route, or pass identity changed. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame recipe, frame graph, or dependency edge changed. The Vulkan smoke added its missing direct import of the owning `RenderGraph` module only. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `RUNTIME-186` and `RUNTIME-187` close at `Operational`: focused runtime/app coverage passed 54/54, checker regressions 22/22, CPU 4,269/4,269, fresh ASan 2,923/2,923, fresh UBSan 2,923/2,923, and promoted Vulkan 48/48 including shutdown LeakSanitizer. No maturity follow-up is owed. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No compatibility shim, layering allowlist entry, warning-mode gate, temporary debt, or unowned TODO was introduced. The convergence policy records `temporary_debt: null`. |

## Findings → follow-ups

- No findings.
