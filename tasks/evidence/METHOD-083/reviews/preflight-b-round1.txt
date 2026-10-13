# Preflight B — actual Round 1 response

Session `/root/method083_preflight_b`, fresh Codex reviewer; read-only/no delegation
and staged blinding instruction-only. No other review supplied before this answer.

Round 1 complete; **no execution verdict yet**.

Reviewed commit `a81051429`, packet digest `c03533d3e9736f289571bb03b995ea2e1e8072b4c7eb5ab3d0a961ce501a613b`. I regenerated the live manifest in memory and confirmed its digest, both engine binary/adapter/geometry bindings, all six fixture/export bindings, and upstream executable/source hashes. Blinding, read-only operation, and no delegation are instruction-only. I did not open Layer B, intent plans, other reviewers’ answers, or result logs.

The actual experiment is:

- **58 native processes:** six full/partial × seed 1–3 cases, nine configurations each, plus four same-seed replays of adapted-union and upstream-Nyström.
- **Engine configurations:** old Auto/FPS/box; adapted Auto/FPS/box/union. “Exact” selects Auto, which may choose Dense, Truncated, or IFGT. It is not a forced Dense reference.
- **Adapted treatment:** regularized Cholesky Nyström solves, an additional union-random option, and explicit transition to bounded Truncated when sigma < .2. Thus old-versus-adapted Nyström changes several mechanisms together.
- **Sampling:** engine G remains rank-100 FPS with unchanged sampling settings. Engine FPS E-step uses per-cloud quotas and fixed identities; box uses resampled positions; union uses one fixed seeded permutation across moving source and fixed target identities, without quotas. Accepted E-step landmark counts may increase from 256, with accuracy/work guards and Auto fallback. Counts and repairs are not fully observable.
- **Upstream references:** J256 or J0, rank-100 random G, and KD below sigma .2 with radius `min(7*sigma,.15)`. The J256 union permutation consumes RNG before G construction; J0 consequently changes G too. Equal numeric seeds do not pair engine/upstream draws. These compare whole native policies, not isolated E-step accuracy.

Inputs are identical representable float32 coordinates serialized as exact-recovering doubles. Source and target are centered independently and divided by a common RMS-radius scale. Truth uses the target center. Endpoints map back using target mean; endpoint RMS/max are original world units. Sigma² is solver units²; scale and spread ratios are dimensionless. NLL is an engine stopping diagnostic, not a common upstream/engine objective.

The engine stops before an M-step on relative objective change; its trace omits that final convergence-only E-step. Upstream stops after an update on absolute sigma or sigma-change below `1e-5`, only at zero-based index >30. Its printed history omits the final converging update; info contains final sigma. The parser’s `loops+1` for convergence and `loops` at cap matches the upstream source. Native “converged” cannot establish equal optimization accuracy.

Endpoint analysis measures:

- Correspondence-indexed truth RMS/max, improvement over original unregistered source, and spread ratio.
- Agreement with same-version Auto: RMS ≤.001 and max ≤.01 world units.
- Adapted-union truth RMS ≤1.01×upstream-Nyström RMS +2e-6.
- Observed/hidden/clutter-replaced/boundary-ambiguous subset RMS and counts.
- Exact replay endpoint/observed-trace equality, excluding timing.

A raw-source identity result fails the 10% improvement screen. A **solver-frame no-op is different**: inverse mapping yields `source − source_mean + target_mean`, a centroid translation that can improve truth RMS and preserve spread without learning deformation. Layer B must distinguish that null from raw identity. Complete collapse fails the spread gate; moderate shrinkage, wrong correspondence structure, or a mediocre solution close to Auto may still satisfy individual screens. All-point indexed truth metrics help expose these failures, but the coarse thresholds are not proof of deformation recovery or identifiability in the hidden region.

Execution is bounded by two fixed CPU slots, 1,800 seconds/process, 8 GiB address space/process, and 54,000 seconds total. Every launch recomputes the manifest. Native timeouts become unevaluated; documented upstream numerical failures remain scientific failures; unexpected failures or malformed outputs halt further work and retain a partial report. Scientific counterexamples otherwise do not cancel the fixed plan.

**Concrete issue found:** [comparison.py:170](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-083/comparison.py:170) substitutes `False` for missing `auto_agreement`. When adapted Auto times out but another adapted arm converges, that arm lacks an Auto endpoint and is counted as disagreement. A read-only known-answer probe of the actual expression confirmed `REFUTED`, where this missing comparison should be `UNEVALUATED` unless another observed condition independently refutes the combined screen. This contradicts the timeout/missing-measurement semantics.

Additional observations for Round 2:

- `stages` is parsed but unused by `report`; HTML marks backend transitions, not the available stage-event chronology.
- `local_transitions` counts flagged iterations, not transition events.
- Layer A specifies two Codex reviews, whereas the supplied default FULL procedure requires Claude + Codex. This needs explicit user authorization in the intent/session packet.
- No upstream NLL/ELBO breakdown, complete deformation trajectory, common stopping rule, population uncertainty, or defensible performance ranking is produced.

Awaiting Layer B. No native experiment or technical run was started; final-config technical/control evidence and intent alignment remain for Round 2.
