# BUG-095 verification observations

The first canonical full-CPU run on 2026-07-31 exited `8` because
`RuntimeSceneLifecycle.RetiredQueuedSceneSavePublishesTerminalEvent` failed its
first registration. The same case passed in its later registration during that
run, and the failure exactly matches open `BUG-123`. The original receipt and
hash-bound logs are retained here; the required command lane contains the
unchanged rerun used for BUG-095 completion.

A later focused verification draft added an invalid assertion against a job
token after `JobService` had already retired its terminal record. The product
contracts passed in that run, but the assertion failed and was removed in
favor of the durable entity-sidecar `Published` assertion already in the
test. Its concurrent repeat invocation also raced CTest discovery in the same
build tree. Both receipts are retained here, and the final focused/repeat
commands were run sequentially.

The first strict clean-workshop rerun after the independent review repair
correctly rejected the moved task because its status used "Implementation
revision" instead of the workflow schema's required `Commit:` field. The task
record was corrected after the exact repair commit existed, and the failed
receipt is retained here rather than overwritten.
