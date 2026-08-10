# Superseded custody attempts

`20260810-run-001-pre-completion-date/` preserves the first non-claim-eligible
scratch run and frozen protocol. Strict task-policy validation then found that
the retired METHOD-037 record lacked its required explicit completion date.
Adding that date changed the task hash bound by the run, so run 001 is retained
for auditability but is not canonical completion evidence and must not be
accepted. The canonical `experiment/` run was initialized only after the task
bytes were corrected.

`failed-ara-run001-paths/` preserves the first required strict-ARA receipt. It
correctly failed because C38 still named the superseded run-001 bundle and
audit. C38 was corrected to canonical run-002 and `commands/ara-claims-final.json`
is the passing required receipt; the failed receipt is intentionally excluded
from the canonical completion report without being deleted.
