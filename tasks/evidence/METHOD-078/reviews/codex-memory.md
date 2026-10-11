**PASS — pilot execution only.**

Commit: `0efb7e48f87ee57fd5cb34f2c5d072fb71391d3e`  
Digest: `e75cf4c61d104b0529f5f1ba1e24a0e8eed6728d1c55053e6ef396aae2f2f229`

The live manifest matches exactly. The [control-test script](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/verification/test-controls.py) now sets both address-space limits to 8 GiB before exec, retaining the 300-second timeout. The [assertion record](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/verification/driver-selftest.txt) records both capped reruns passing. **The outstanding blocker is resolved.**

The diff is limited to the script, assertion record and Layer B clarification. Native binaries, cells, numerical gates and budget are unchanged. Earlier five technical checks are accurately identified as uncapped; their success under the pilot’s memory ceiling remains prospective and enforced by the driver.

No outstanding blockers from this review. Both reviewers must cover this new digest before execution; Claude’s prior-digest approval does not automatically transfer.

Skipped result metrics and unrelated checks; no writes, delegation or experiments occurred. This PASS permits neither production adoption nor a performance claim.

Rendering note: source-line suffixes were removed from file links for the repository validator. [Verbatim answer](codex-memory.txt).
