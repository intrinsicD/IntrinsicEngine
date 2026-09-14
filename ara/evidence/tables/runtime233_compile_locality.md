# Density/spacing compilation-locality pilot

[RUNTIME-233](../../../tasks/done/RUNTIME-233-processing-compilation-locality-pilot.md)
separates the shared execution context, typed family records and session
composition. [C92](../../logic/claims.md#c92-processing-family-locality-is-a-prototype-latency-hypothesis)
is a performance hypothesis. These local measurements are single samples from a
dirty worktree and are explicitly **not claim-eligible**.

## Controlled edit probes

Same workstation, Clang 23, Debug `ci` preset, eight build jobs, disabled ccache,
preinstalled vcpkg dependencies, unflushed filesystem cache. Each content edit
was restored and rebuilt before the next probe. The command built
`IntrinsicRuntimeContractTests` and `ExtrinsicSandbox`; tests did not run during
timing. Isolated Ninja append logs count physical compiler invocations, merging
a module's object/BMI outputs. No touch-only or mixed-history comparison.

| Content edit | Before build | Candidate build | Before/candidate compilers |
|---|---:|---:|---:|
| Spacing runtime body | 13.303 s | 10.763 s | 1 / 1 |
| Geometry algorithm body | 7.882 s | 7.933 s | 1 / 1 |
| Processing panel body | 17.446 s | 17.229 s | 1 / 1 |
| Public spacing-result record (after review) | 399.339 s | 61.906 s | 62 / 14 |

The first candidate still imported family sinks through the shared test fixture:
its record probe took 172.178 s and 41 compiler invocations. Keeping those sinks
local to their two tests reduced that probe to 77.103 s / 23 compiles. Claude
then identified complete family containers in shared private workspace bindings.
Forward-declared borrowed containers removed nine sibling scene, visualization,
recipe, workspace and context-adapter compiles. The review repeat takes
61.906 s / 14 compiles: 11 family/runtime/app producers and three family/session
tests. The extended compiler gate enforces this boundary. Only the public-record
probe was repeated after review; the body rows retain their earlier samples.

For the record probe, the Sandbox executable linked at 398.174 s before and
52.066 s after review; the contract-test executable linked at 399.330 s and 61.897 s.
These are build endpoints, not application-startup measurements. Algorithm and
panel samples offer no meaningful performance conclusion at this sample count.
No clean-build timing was measured.

## Verification and size

| Gate | Outcome |
|---|---|
| After-review focused behavior and dependency checks | 73 selected, zero failures |
| Full unsanitized CPU selector | 4,538 selected, zero failures, six capability skips |
| After-review affected ASan selector | 55 selected, zero failures or skips |
| After-review affected UBSan selector | 55 selected, zero failures or skips |
| Actual Vulkan LBVH processing smoke | Nine selected, zero failures or skips |

Before review, the full separate ASan and UBSan selectors each passed 2,916
grouped/individual entries; their original results remain in the diagnostic
record. After repair, the full CPU selector and affected sanitizer/Vulkan
selectors above passed on the final source.

The GPU cases retain cross-domain publication, stale-input rejection and
cancellation after submission. New CPU/ASan/UBSan tests cover freeing the scene
while work is queued, result copies, dismissal and expired attachment callbacks.
Strict layering, task policy, doc links, docs synchronization and skill mirrors
pass. The initial sandbox-only ASan discovery failure is tracked separately as
[BUG-188](../../../tasks/backlog/bugs/BUG-188-sandbox-sanitizer-test-discovery.md);
the identical host retry passed without changing sanitizer settings. Existing
root metadata hygiene remains [BUG-177](../../../tasks/done/BUG-177-root-hygiene-local-agent-metadata.md).

Across the 47 affected production files, physical lines increase from 44,763 to
44,973: **six new files and 210 net lines**. This includes interfaces, compiled
owners, private access, caller wiring and build declarations. Shared capture is
compiled once, obsolete family entries and overload pairs are removed, and the
new execution/assembly boundary adds code. This is not net engine slimming.

Claude's design and implementation reviews are complete. The source-confirmed
coupling was repaired and checked locally; the task note records every finding's
disposition. The pilot is ready for landing. Further families belong to
RUNTIME-234/235, and Framework24 product completion remains a separate gate.
Nothing was committed or pushed.

## Evidence custody

[The diagnostic record](../diagnostics/runtime233_compile_locality.json) embeds
the exact manifest, all thirteen validated schema-v2 results, source identities,
compiler source lists, build/link endpoints, footprint and verification log
hashes. Full local logs, the frozen runner, isolated Ninja histories and pilot
diff remain under `build/analysis/processing-locality-2026-09-12/`. That ignored
local directory is not a portable, clean-source benchmark cohort. Promote no
repeatable performance claim from these samples.
