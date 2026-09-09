# Primitive selection implementation verification

Recorded 2026-09-08T12:18:27+02:00. Local dirty checkout based on `314db9ea5be314c732458649775103c461992fbb`.
This is an implementation verification record, not a benchmark or a claim of
Framework24 parity or Vulkan pixel behavior. Prior geodesics evidence remains
bound to its earlier source state.

| Check | Result |
| --- | --- |
| `cmake --preset ci` | Passed; Clang 23 |
| `cmake --build --preset ci --target IntrinsicTests ExtrinsicSandboxEditor IntrinsicSandboxEditorIntegrationTests` | Passed on final source |
| Focused CTest selection below | 145 passed, 0 failed; 2.55 seconds |
| Default CPU CTest exclusion selector | 4331 passed, 6 skipped, 0 failed; 103.51 seconds |
| Strict layering, test layout, task policy, explicit changed-file docs sync | Passed |
| Documentation link check and diff whitespace check | No findings |

Focused selector:

```bash
ctest --test-dir build/ci --output-on-failure -R 'PrimitiveSelection|SceneInteractionModule|SelectionController|SelectionSnapshotExtraction|SandboxDomainPanels|GeodesicsOperations|SandboxEditorGeodesics|ParameterizationOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

The full CPU run used the same exclusions and timeout without `-R`. Five skips
require a display; `GlfwLifecycleLsan` requires its sanitizer instrumentation.
No sanitizer run or Vulkan pixel/readback run was performed for this change.
Coverage includes ordered set editing, deleted/stale indices, graph/point-cloud
domains, production input/readback hooks, modifier capture, batch ordering,
method consumption, config preview/apply, expired editor attachments, and
world-qualified renderer highlight snapshots.

## Observed source hashes

| File | SHA-256 |
| --- | --- |
| `src/runtime/Scene/Runtime.SelectionController.cppm` | `e6cdeef03b5e5176d92a44f3953f9709da2c568063bdd6b386601133de1c5cdb` |
| `src/runtime/Scene/Runtime.SelectionController.Primitives.cpp` | `e101915d3775831808544785b28b26c88b723f867e4a57dacb6ce5812b169cbd` |
| `src/runtime/Scene/Runtime.SelectionController.Config.cpp` | `1bd096a5bd9b85e4c0ac4d7f864244ad0cde125a7dcad9e908e9f750cbca1481` |
| `src/runtime/Scene/Runtime.SceneInteractionModule.cpp` | `f894a66e78f51158ad24668d0dc5e8235175541bacd9e2813f4d3c848b14619a` |
| `src/runtime/Scene/Runtime.SceneInteractionModule.Selection.cpp` | `9937751f0a79b7c518f95e987ad6daea183e2cca388ef8ec3bc8ddfc2fd48454` |
| `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Public.cpp` | `675a39d04a11125dc0ccc75c60dc0edd2adf36140cc1480146216e869258bd2e` |
| `src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp` | `565301807c916f67f2522dbed49cedc85d5b8211181e912e92ae115a2c609e35` |
| `src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp` | `ede7da84d66fef856321849b6a149a2ff3152fcb5773649f714637ced9849091` |
| `src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp` | `c9dad024dab83880d3f9a61446dc978ad716773ec551561d05a711b7e1a28216` |
| `tests/contract/runtime/Test.PrimitiveSelection.cpp` | `0f418cb24d1495f7241aad95058bf46f6a0798ef94a7be7208f53cda7012cb8d` |
| `tests/contract/runtime/Test.SceneInteractionModule.cpp` | `71933ceb2cc13878081662f497ae1b6c2fb0551d332b47ecb45f6694597753c7` |
| `tests/contract/runtime/Test.SelectionSnapshotExtraction.cpp` | `0774e3f3f552c8a5d83bade9cc54ee7dac28cd106e2872217ad96c961984381c` |
