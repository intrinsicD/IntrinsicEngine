# CI-007 PR-fast ccache cohort

All unchanged-source samples used commit `5394e51b`, `ubuntu-24.04`, Clang 20,
the `ci` preset with project-default ASan/UBSan, 3,549 selected tests, 1,955
Ninja command edges, and an exact vcpkg cache hit. GitHub Actions links are the
authoritative hosted artifacts; downloaded copies were strictly validated on
2026-07-13.

| Population | Run | Build (ms) | Total (ms) | Hits | Misses | Errors |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold | [29113978973](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29113978973) | 1,111,716 | 1,283,257 | 0 | 575 | 0 |
| Cold | [29115473396](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29115473396) | 1,510,259 | 1,753,939 | 0 | 575 | 0 |
| Cold | [29117419922](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29117419922) | 1,391,575 | 1,616,213 | 0 | 575 | 0 |
| Cold | [29119188858](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29119188858) | 1,406,634 | 1,634,116 | 0 | 575 | 0 |
| Cold | [29127596925](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29127596925) | 1,458,197 | 1,699,530 | 0 | 575 | 0 |
| Warm | [29208730070](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29208730070) | 591,978 | 833,515 | 575 | 0 | 0 |
| Warm | [29209234114](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29209234114) | 609,476 | 856,085 | 575 | 0 | 0 |
| Warm | [29209736454](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29209736454) | 630,449 | 877,683 | 575 | 0 | 0 |
| Warm | [29210235661](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29210235661) | 436,073 | 605,443 | 575 | 0 | 0 |
| Warm | [29210606175](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29210606175) | 623,089 | 867,701 | 575 | 0 | 0 |

Nearest-rank statistics for five samples:

| Population | Build median / p95 (ms) | Total median / p95 (ms) |
| --- | ---: | ---: |
| Cold | 1,406,634 / 1,510,259 | 1,634,116 / 1,753,939 |
| Warm | 609,476 / 630,449 | 856,085 / 877,683 |

Warm reductions were 56.6713% build median, 58.2556% build p95, 47.6117%
total median, and 49.9593% total p95.

The repository-interface sample used evidence-only commit `4befbe1e`. Hosted
run [29211278659](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29211278659)
restored the compatible store from `5394e51b`, then recorded 919,382 ms build,
1,064,002 ms total, 0 hits, 575 misses, and 0 errors. It passed all 3,549
selected tests and the hermetic cached/clean parity probe. Commit `fd97d4d1`
removed the temporary exported API marker.
