---
id: BUG-195
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Host verification reliability incident; no performance or capability claim.
contract_schema: 1
contracts: []
contract_review: Existing build and verification hygiene applies; no new engine contract.
---
# BUG-195 — Verification can exhaust host disk space

## Goal
Add actionable disk-headroom handling to local multi-preset verification so
builds cannot silently lose test logs and result records when the volume fills.
Do not delete source, datasets, active build artifacts or diagnostic records.

## Evidence
RUNTIME-247 on 2026-09-14 filled the 1.8 TB root volume while reconciling native,
ASan, UBSan and Vulkan builds. The sandbox could not create its temporary mount
(`No space left on device`). CPU CTest and UBSan build wrappers could not write
their JSON completion records; the full CPU log and benchmark JSON were truncated.
Those interrupted/incomplete records are not accepted as passing evidence.
Logs: `/tmp/intrinsic-keypoint-vulkan-20260914/` (`full-cpu.log`,
`reconcile-ubsan.log`, `raw-benchmark/keypoint-compute.json`).

No build processes remained when cleanup was inspected. Removing only compiled
objects/libraries and ELF executables from inactive `ci-no-ccache`, `ci-clang20`,
`ci-coretasks-clean` and `ci007-interface-no-ccache` trees reclaimed about 5 GiB.
Their configuration/logs, all source and all datasets were retained. Required
verification must be rerun with complete output; cleanup does not close the
missing preflight/recovery behavior.

## Acceptance criteria
- [ ] Detect insufficient headroom before expensive local verification, with an actionable diagnosis.
- [ ] Preserve completed evidence and report failures to write result records explicitly.
- [ ] Document a conservative policy for reclaiming inactive generated artifacts.

## Verification
Reproduce a write failure on a bounded temporary filesystem or injected writer,
without deliberately filling the host volume. Check that the runner reports
an incomplete evidence record and retains earlier completed output. Re-run the
original native/sanitizer selectors with sufficient headroom; do not weaken them.
