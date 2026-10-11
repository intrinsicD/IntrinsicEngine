Independent read-only code review. Do not modify files or delegate. Only Read/Grep/Glob tools are enabled.

User questions/instructions verbatim:
1. "Also heißt das identifizieren und exakt nachrechnen?"
2. "Dann versuchen wir es damit mal."
3. "Mach dann nochmal einen Test bitte"

Operator-directed shared CPD/BCPD E-step repair outside Framework24 P0; no separate goal file. Scope is the complete-row repair, preserving approximation/error/cost limits; no universal positivity claim.

Object: tasks/evidence/METHOD-081/reviews/code.patch SHA256 a2f0d3daa82a58f116b1982a478ea241d57e26220114cdfdab0f8c60c8b3af4b, against HEAD a93dcb8c7. Current four touched files match this diff and remain fixed during your review. Read the patch and relevant surrounding source/tests. Ignore temporary benchmark runner instrumentation and research bookkeeping.

First check alignment with user intent. Then do a ponytail complexity review and independent correctness review: weighted exponent shifts, shared ExternalRows behavior, SourceTree lifetime, excluded h contributions, exact P1/PX merge, source validation after merge, sampled exact-row bypass, budget across retries, and accepted/rejected kernel accounting. Verify the two new tests can fail when the mechanisms are removed. Relevant logs: tasks/evidence/METHOD-081/verification/regression-before.log, regression-after.log, mutations.log, each mutation's *-test.log, focused-tests.log (may still be completing). Current focused commands: cmake --build --preset ci --target IntrinsicGeometryRegistrationSlowTests; ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(CoherentPointDriftAccelerated|CoherentPointDriftBayesian)\.'

AGENTS.md governs boundaries; code remains private implementation inside geometry and adds no public API/config. Return approve | revise required, each required finding with file:line and concrete failure. State which engineering acceptance criteria you accept, separately from the not-yet-run experiment. Avoid new features or broader numerical-policy changes. English, <=1200 words.
