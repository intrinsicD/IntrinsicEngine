# GEOIO-004 implementation review

Claude Sonnet 5 at medium effort independently reviewed the fixed extraction
and seven public regression tests, read-only, and found no actionable defect.
The parser retains the lexical loop; mesh-only list-count validation moves to
its public caller while the cloud body parser keeps its own policy. Both
loaders preserve public errors, ordered records and the byte offset immediately
after `end_header`. Records own their strings; the original file text outlives
all body parsing. The global-module-fragment declarations match the new ordinary
compilation unit, and both loaders link in the focused executable.

Reviewed source: `434cb0e35442427ab4fac52a6cd6e2ada6aff0ff`.
Final source: `478e32e0a27b9f7569710d8ddd0f503c5840d60a`.
The only post-review correction moves the CMake source entry beside
`Geometry.IO.cpp`, as suggested. It changes neither target ownership nor code;
no substantive re-review was needed. Full verification uses the final source.
Final code/test/owner-route patch SHA-256:
`ad50d7f30833ae2bbe98acf6fe49aafe68cf92c2742d09d423b3a6f435f28a9b`.

All 224 focused GeometryIO cases pass on the original and extracted code.
The seven new cases cover scalar aliases and lexical rules, malformed/truncated
headers, the ASCII cloud floating-count exception, repeated properties and
loader-specific repeated vertex elements, and CRLF binary offsets in both
endians. Existing body, attribute, exporter and routing assertions remain.
Full CPU and sanitizer results and compile measurements are recorded separately.

The complete implementation footprint, including the helper, declarations,
callers and CMake entry, is 5,921 → 5,847 lines (-74). The additional tests add
150 lines; total affected code is 12,040 → 12,116 (+76). No formatting compression
contributes to the reduction. The source-documentation audit reports no objective
errors; its two size hints concern the existing mesh/cloud IO owners. Their
format/body functions remain discoverable and are outside this extraction.
The private helper's synopsis and canonical owner route were updated; docs-sync
and skill-mirror synchronization pass.

The first build exposed a mechanical insertion at both the forward declaration
and definition of `ByteSwap`, creating a duplicate header record before its
required element type. That source and failure are retained. The record now
appears once, after `PlyElement`; the corrected source passed focused tests
before independent review. This is an introduced and corrected implementation
error, not a pre-existing repository blocker.

On the final source, the expanded focused selection passes all 319 tests.
The full ci run selects 4,886 entries and fails only BUG-206; the full ASan run
passes all 3,237 entries, including the GLFW lifetime check. The full UBSan run
selects 3,237 entries and fails only BUG-206, with the expected GLFW skip.
Both failures are the unchanged queued/running message comparison for the same
job identity, with no sanitizer diagnostic. The [blocker record](../../backlog/bugs/BUG-206-uv-duplicate-submit-phase-race.md)
retains baseline and unsanitized reproductions. No unrelated fix or weakened
assertion is included. All strict structural gates pass; the workflow-evidence
validator retains its 105 historical warnings and reports zero errors.

The frozen compiler-work gate subsequently rejected the candidate. All task source and test changes are restored; the canonical ci IO build and all 217 original IO cases pass. See the negative measurement decision for exact ranges and archived candidate identities. This review describes the rejected experiment, not a retained parser.
