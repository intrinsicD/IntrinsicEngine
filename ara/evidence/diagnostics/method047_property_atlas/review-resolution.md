# METHOD-047 review resolution

Claude Opus 5.5 reviewed fixed source revisions in five passes. The reports preserve
what each reviewer saw, including verification that was pending at that time.

- Review 1 (`947438ca9`): polygon publication, bake resolution/readiness,
  canonical UV precedence, config precision, raw values and paired-asset cleanup.
  Addressed in the review-2 snapshot and bake remediation report.
- Review 2 (`d32aa779c`): permanent oversized-source/failed-asset rejection,
  exact atlas extents, reconstruction's optional UV enrichment, authored corner
  preservation, thin-chart GPU evidence, region-edge allocation and run provenance.
  Addressed in `ad532bf39` and the metadata remediation report.
- Review 3 (`ad532bf39`): stale extent removal broke UV undo. Addressed in
  `b814eb87` by retaining the content binding and caching negative revision verdicts.
  Canonical corner UVs also ignore shadow vertex UV edits.
- Review 4 (`b814eb87`): confirmed both fixes and found a nested-history case:
  regenerate, parameterize, regenerate, undo twice. The final correction retains
  a stale extent in the regeneration before-state and restores its original
  fingerprint with cleared revision stamps. Current after-states still publish
  and rebind normally. The extended history regression exercises undo and redo
  through this sequence; the final 198-case focused CPU run passed.

The root reviewed the final history correction against Claude's prescribed fix.
No geometry admission, distortion threshold, test assertion, sanitizer gate or
Vulkan capability requirement was weakened to obtain passing results.

Architecture review: layer imports and CMake links pass strict checks; no
allowlist exception exists. Public surfaces stay within their owning layers.
The existing UV-view and texture-bake owners contain renderer growth. Typed
frame-recipe passes and resource dependencies remain authoritative; ImGui after
Present is an explicit backbuffer-composition dependency documented in ADR-0012.
There is no scaffold-only closure. Final execution evidence is recorded separately.

A subsequent Opus 5.5 results audit checked the retained numerical campaign and
report draft. Its report is `claude-results-audit.md`. The root corrected the
three independently rounded maxima, removed the unsupported final-binary 1024²
child rejection, clarified that the 1e-6 tolerance applies to acceptance against
limits, and reported None's chart-count cost. Backend identity is described as
runner-reported, and old loose summaries are explicitly named `intermediate-*`.
The source-tree comparison was independently recomputed with temporary Git
indices: the sole post-corpus build-input difference is the added GPU workspace
test. Final GPU publication and visual claims remain contingent on their named
execution evidence, rather than the earlier 44-test run.


The final bounded source review at `ae8c1d416` returned PASS for the five-file
R4-1/history and GPU-fixture delta, with no new reproducible defect. It also
independently confirmed every numerical wording correction and the Git-tree
binding. `claude-final-review.md` preserves that verdict and explicitly leaves
final GPU execution and screenshot publication pending; those gates are closed
only by the subsequent execution artifacts.

## Architecture scorecard

| # | Disposition | Evidence and scope |
| --- | --- | --- |
| 1 | pass | Strict layering checks cover 8,614 import/include references; no violations or allowlist entries. |
| 2 | pass | All 84 scanned CMake link references respect the layer table; no new external dependency. |
| 3 | pass | Geometry exports geometry/core data; graphics exports RHI/asset-ID views; ECS and service ownership stay in runtime. |
| 4 | pass | Bake buffers, coverage and retained UV targets extend the existing PropertyTextureBake and UvView owners. |
| 5 | pass | Bake, UV-view, ImGui and presentation work use the existing typed frame-pass identities. |
| 6 | pass | Resource dependencies drive baking/UV work; the explicit ImGui-after-Present composition constraint is documented in ADR-0012. |
| 7 | n/a | This closes an executed Operational path, with CPU-only solver and finite/device-specific evidence boundaries stated explicitly. No scaffold or deferred GPU solver is advertised. |
| 8 | n/a | No migration exception, allowlist row or temporary shim is introduced. |

The final full Vulkan run passed all 97 entries without skips. The separate
task-window screenshot was inspected after the normal gate completed. These
artifacts close the execution gates left pending by the final source reviewer.
