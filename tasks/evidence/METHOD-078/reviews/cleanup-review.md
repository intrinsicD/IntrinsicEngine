# METHOD-078 independent cleanup and claims review

Reviewer: `/root/nystrom_results_review`, fresh independent results reviewer; read-only, no delegation. Actual answer:

What this change does: It restores the original production implementation and preserves the rejected candidate as evidence. The bounded pilot findings remain documented without claiming a production improvement.

**Verdict: Ship. All three METHOD-078 acceptance criteria PASS.**

1. **Rejection causes distinguished:** the independent audit identified numerical, sampled-error, cost-limit and cached rejection separately.
2. **Candidate evaluated without weakened controls:** C119, `audit.md` and `audit-record.json` agree with my independent raw-data verdict. Unconditional retries remain rejected.
3. **Temporary changes removed:** all three production files are byte-identical to `bbdf1af79`. `candidate.patch` exactly equals their experimental diff from that base to frozen commit `0efb7e48f`. The cleanup log records **28/28 original Accelerated tests passing** in `build/ci`.

Ponytail review found no unnecessary production mechanism, residual probe, new interface or unsupported compatibility path. Removing the candidate-specific live regression test is appropriate because its proposed behavior was rejected; the complete test remains preserved in the patch.

All 66 raw files and the frozen HTML report retain their previously reviewed hashes. The strict ARA validator now passes with 119 claims. No findings require correction before recording the verdict and retiring this bounded diagnosis.

Exact SHA256 bindings:

| Artifact | SHA256 |
|---|---|
| Cleanup diff against `0efb7e48f`, three production paths | `4ebbf0922b016b3403235cc3445ea7dc2235418bdb8f9b703e8ab388411deb3c` |
| `candidate.patch` | `1ed6d6d879c912d83aa0e10ca402e8fa3b635db0adb72e42cb0f054310683f79` |
| C119 stanza, including trailing newline | `2bda11f237173f12ef6f7976d78d1692b92175bd1bbf4fc0c5fc1da7a008773e` |
| `audit.md` | `d5f436da49e64c25bc7f6acf7e46549a8b132147ffffbb25263b3035f5243bd5` |
| `audit-record.json` | `0a22e24b99b3fbf0192026a706169a6a18d526aca121efdfe8f3847558ad83f4` |
| `verification/cleanup-tests.log` | `736130caab23807ebed7c5efa19644443b79d2b81c0d2fc4f5fb6e7fd35eddf3` |
| `raw-sha256.txt` | `c87f162607b11b3ff966c02263b09a7ea2e6dad637a9dddad2c50cf84546bac8` |

Not checked: I did not rerun builds or tests; I inspected their supplied current log. Further numerical changes, larger budgets and broader performance claims remain outside this approval.
