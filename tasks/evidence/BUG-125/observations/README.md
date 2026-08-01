# BUG-125 diagnostic command observations

Failed diagnostic receipts are retained byte-for-byte outside the completion
command set. Their stdout and stderr logs remain at the hash-bound paths stored
inside each receipt.

- `strict-task-policy.json` records the first retirement check, which correctly
  rejected the done-task record before its required pending commit reference
  was added. `commands/strict-task-policy-fixed.json` is the passing acceptance
  receipt after that metadata repair.
