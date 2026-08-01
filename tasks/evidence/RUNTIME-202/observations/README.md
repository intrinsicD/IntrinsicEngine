# RUNTIME-202 diagnostic command observations

Failed diagnostic receipts are retained byte-for-byte outside the completion
command set. Their stdout and stderr logs remain at the hash-bound paths stored
inside each receipt; moving a receipt does not rewrite its command, required
flag, exit code, timestamps, or log hashes.

- `ci-full-cpu.json` records the exit-8 default CPU run that exposed the two
  schedule-sensitive queued-import contract failures now tracked by
  `BUG-125`. Both exact cases then passed 20/20 direct repetitions before the
  combined repetition reproduced them.
- `bug125-targeted-repeat20.json` records that combined exit-8 repetition. Its
  deterministic interlock repair and passing stress receipts are owned by
  `BUG-125`; a separately labelled complete passing sweep is the RUNTIME-202
  acceptance receipt.
