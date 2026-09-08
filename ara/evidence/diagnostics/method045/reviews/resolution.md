# Source/result review resolution

The operator approved sharing the unpublished source and mesh-free aggregate
results on 2026-09-07. Claude reviewed the retained packet without tools or input
geometry. Its response is design/source inspection, not an independent rerun or
visual acceptance. Codex checked the findings against local source and arrays.

- **Whole-chart reflections:** confirmed, not an unexplained solver failure.
  The installed xatlas source (revision directory `5c085faf0c-a101c9703d.clean`,
  `source/xatlas/xatlas.cpp`, placement branch near line 8592) swaps UV axes
  when `best_r` is set. This has negative determinant. The Python shelf packer
  preserves winding; the subsequent native packer can mirror an entire chart.
  `compare_atlases.audit` explicitly counts uniformly negative charts and
  normalizes a temporary copy for distortion measurement. It rejects mixed
  signs and degenerate triangles. The saved-array
  [orientation audit](../verification/packed-orientation-audit.json) reproduces
  5/6 sculpt, 9/23 frog, 10/15 fandisk and 9/16 dolphin mirrored charts, with no
  mixed-or-zero chart. The recorded metric and exports are unchanged. This is
  geometric UV validity, not an orientation-preserving packing guarantee.
- **Region preservation:** state it is by construction; zero lost edges and
  immutable labels are correspondence checks, not independently improved
  segmentation. Retain checks because they can catch implementation and export
  regressions even when the algorithm enforces the invariant.
- **Distortion:** explicitly per-chart area normalized, excluding between-chart
  texel-density variation. Native packing's integer extent expansion can alter
  density and shape; the existing post-pack audit reports both. Do not claim a
  causal attribution for every observed density difference from correlation.
- **Shared validity flag:** numerical orientation/nondegeneracy and overlap
  validity do not imply disk topology. Candidate construction requires disks;
  `non_disk_charts` remains a separate comparator diagnostic.
- **Frog quality:** keep 23 candidate versus 15 earlier-prototype charts and
  jagged boundaries visible in the report. Preserving tiny baseline regions is
  not evidence of anatomical quality. No chart smoothing was performed.
- **Annulus:** this cut plus free-boundary solver rejected one chart; that is
  not proof that an annulus cannot be opened with another valid construction.
- **Settings/toolchain:** reported occupancy uses resolution 1024 and padding 2;
  the retained configure log identifies Clang 23, valid under the repository's
  minimum Clang 20 contract. No replacement with a remembered compiler version.

These resolve the requested reporting clarifications without changing the
algorithm, saved charts, acceptance limits, or production adoption status.
