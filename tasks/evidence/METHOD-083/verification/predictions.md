# Test predictions

Before the first run: the dependent-landmark oracle must fail on the pre-ridge engine;
all E-step and config invariants must pass after adaptation. The before-test log records
the expected ridge failure. The first focused run passed 60/61 tests.

Correction: the unchanged-policy test initially asserted Auto always chooses Dense for
coincident points. Its existing planner legitimately chooses FastGauss. The corrected
assertion compares the exact policy and statistics with the transition disabled, which
is the intended unchanged-policy contract. No production behavior or tolerance was weakened.

Mutation assertions: removing the union branch must fail the union identity/quota test;
removing the sigma switch must fail its boundary/diagnostic test; removing exact-row repair
must fail the regularized, weighted sufficient-statistic oracle. Each mutation is restored
before the next, and the final focused suite must pass.
