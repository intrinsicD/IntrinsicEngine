# Superseded pre-seal validation

The first repository-global workflow-evidence receipt ran after `BUG-150` had
moved to `tasks/done/` but before its own completion report and historical seal
were generated. It correctly failed because the retired task temporarily had
no required report. The receipt and raw logs remain here as diagnostic
provenance; the canonical required receipt is recorded only after the report
and clean-HEAD seal are committed.
