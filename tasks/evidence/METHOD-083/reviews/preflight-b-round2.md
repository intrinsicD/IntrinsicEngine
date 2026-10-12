# Preflight B — actual Round 2 response

Session `/root/method083_preflight_b`; independent and read-only, no other review read.

**REVISE** for commit `0e37ea222721eddeb9b41a79dea26b4bd33898f6`, manifest digest `3f19fe4028f3d59d15d831b4ff6fba75973ef461aa7b9f5625f191d0c4d465e5`.

The run serves Q1:

> “Bitte pass unsere implementieren entsprechend dieser Punkte des Original Codes an. Dann wiederhole den Vergleich, auch mit dem Original.”

The old/adapted controls, unmodified original executable, fixed full/partial cases, and limited replays address that request directly. The retained guards and native-policy differences are disclosed. The trusted “Ok ohne Claude” instruction authorizes the two-Codex substitution. I found no separate goal file or repository LESSONS file.

The live manifest matches the supplied digest. I did not inspect the other reviewer or `reviews/`. Read-only, blinding, and recursion protection remain instruction-only.

Two blockers remain:

1. **Unavailable replay pairs can abort reporting or masquerade as drift.** In [comparison.py:147](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-083/comparison.py:147), `equal_trace` indexes the first attempt’s trace before checking endpoint availability. A successful replay after a first timeout with an empty trace raises `IndexError`; I reproduced this using the actual expression, without running a native process. If the censored first trace is nonempty, the later expression records `False` because its endpoint is missing. Both cases should produce an unavailable replay result, not technical failure or measured drift. Test empty/nonempty censored originals and a censored replay.

2. **The final-configuration technical evidence covers a prefix, not terminal execution.** [technical.py:30](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-083/verification/technical.py:30) accepts the first completed-step marker and then kills the process. The terminal adapter test uses 64 points, rank 0, and one iteration; the report/controller tests use synthetic outputs. This does not establish terminal execution with the actual 100k/rank-100 configurations. The [experiment-preflight skill](/home/alex/.codex/skills/experiment-preflight/SKILL.md) explicitly requires: “The real code path runs end to end with the final configuration.” Supply that technical evidence without treating numerical outputs as results, or obtain an explicit user exception to this requirement.

The corrected controller changes themselves are appropriate: frame-aware initialization is included in the null comparison; missing Auto now preserves three-valued logic; upstream consumed metadata must be finite; replay comparison includes the local flag and final state; stage events are rendered; `local_iterations` is accurately named. The bound checks and removal mutations substantiate those fixes. I accept these corrections individually, but not the complete controller block while blocker 1 remains.

| Checklist | Assessment |
|---|---|
| 1. Question/configuration | Pass: all cells serve Q1. |
| 2. Units/reference scales | Pass: solver/world frames, sigma², endpoint thresholds, and export margin are explicit. |
| 3. Borrowed implementation | Pass: original Cholesky ridge, union sampling, sigma switch, and retained differences reconstructed from source. |
| 4. Separating coincident conditions | Pass: transition-boundary test distinguishes sigma from sigma². |
| 5. New-data predictions | N/A: frozen existing fixtures; no newly calibrated thresholds. |
| 6. Raw examples | Existing inspection is recorded; report renders first/last samples. |
| 7. Known-answer mechanism | Pass: ridge, weighted repair, union identities, switch boundary, and mutation evidence. Gradients are inapplicable. |
| 8. Information flow | Pass: truth-swap adapter test leaves positions unchanged; mechanism-removal tests change expected behavior. |
| 9. Teacher before student | N/A: no learned teacher target. |
| 10. Target leakage | Pass: truth is read only for diagnostics after registration; hidden-region identifiability remains limited. |
| 11. Sampler coverage | Pass for the declared finite plan and implemented variants. |
| 12. Degenerate solutions | Pass for specified screens: raw/frame no-op, collapse, and shared wrong endpoints are rejected. |
| 13. Ablation validity | Pass as whole-policy comparison; upstream direct/Nyström is explicitly not an isolated ablation. |
| 14. Missing measurements | **Revise:** unavailable replay pairs mishandled. |
| 15. Metric validity | World-unit indexed errors and subset counts are appropriate. Directional bias is not characterized. |
| 16. Whole-evaluation controls | Substantial evidence; add missing replay-pair controls. |
| 17. Replication/uncertainty | Adequate for six descriptive cases, not population inference. Replay drift limits must govern interpretation. |
| 18. Matched comparisons | Engine G/input pairing preserved. Cross-program and upstream-J RNG differences explicitly limit causal conclusions. |
| 19. Budgets/base task | Matched finite caps support a bounded diagnostic, not assurance that every arm learns; timeouts remain censored. |
| 20. Tuning/evaluation separation | No tuning or generalization claim; historical fixture reuse disclosed. |
| 21. Selection rules | Pass: fixed native endpoints, per-case screens, aggregates and thresholds. |
| 22. Random distributions | Union uses a seeded partial Fisher–Yates permutation; identity/quota behavior is checked. No population-distribution claim. |
| 23. Final-config execution | **Revise:** prefix-only evidence. |
| 24. Enforced resources | Time/address-space bounds and prefix memory checks exist; replay censoring needs correction. |
| 25. Runtime prediction | N/A: no speed claim. |
| 26. Shared resources/output | Two disjoint CPU sets, capped threads, fresh output directory and retained outcomes are appropriate. |

Material limits remain: Auto is not forced Dense; G bases and stopping rules differ; upstream J changes also alter G draws; partial hidden truth is not necessarily identifiable; neither P1 nor P3 establishes truth recovery; full ELBO and deformation trajectories are unavailable. Three seeds and four replays support only the declared descriptive comparisons.

I ran manifest verification and a read-only replay-expression probe; I did not run native experiments or rerun tests that write files. No scientific outcome or execution PASS is issued.
