# Point LBVH integration review

Scope: shared entity point indices and the existing Vulkan k-means assignment
consumer. Earlier local geodesics and selection work is preserved. The
[source-bound verification record](../../ara/evidence/tables/point_lbvh_verification_2026-09-08.md)
and [C76](../../ara/logic/claims.md#c76-bounded-cpu-and-vulkan-point-lbvh-integration)
bound the tested result.

| Clean-workshop row | Disposition |
| --- | --- |
| 1. Promoted imports | Pass: strict scan has no violations; geometry owns CPU kernels, graphics owns RHI workspaces, runtime owns ECS resolution |
| 2. CMake links | Pass: existing legal geometry/core, graphics/RHI and runtime composition dependencies |
| 3. Public types | Pass: graphics APIs expose GPU views and RHI handles; ECS/world identity appears only in runtime |
| 4. Renderer growth | Pass: concrete compute workspace; no renderer member or central frame-loop branch added |
| 5. Typed frame-pass IDs | N/A: compute recording through the existing method participant; no new frame-recipe passes |
| 6. Recipe dependencies | N/A: frame composition unchanged; explicit compute-buffer barriers order the LBVH and method dispatches |
| 7. Maturity closure | N/A: no task retirement or broad parity gate closure |
| 8. Exceptions | Pass: no layering exceptions or temporary compatibility shims |

One concrete cache also implements its runtime-module lifecycle; no service
forwarding chain, ECS GPU component or separate scheduling framework is added.
The entity cache and moving-centroid workspace are separate current lifetimes
sharing the same lower-level implementation. Cache handles are checked against
world/entity identity and canonical position/deletion revisions on use.
GPU callers own submission and result-buffer retirement; cache shutdown
precedes device destruction. Geometry/property publication remains in the
existing clustering transaction.

All k-means shader push layouts now include the 64-bit node address and match
the 40-byte C++ block. LBVH build and query blocks are each 64 bytes. Shader
compilation and actual Vulkan readback pass. The existing clustering config
and UI select CPU reference or Vulkan compute; successful Vulkan diagnostics
identify LBVH assignment and are displayed without a fallback label.

Final review corrected CPU rebuilds from a borrowed subspan of the prior
snapshot and ensured the cache records the device that owns its lazy GPU
allocations. The CPU gate was repeated after those fixes. The last UI change
only corrects diagnostic wording and was compiled into the final Sandbox.

The results audit makes no speedup or full Framework24 parity claim. The CPU
smoke is dirty-source and non-claim-eligible. GPU bitonic sorting, range-bound
unions, small-k costs and large-input scaling require separate comparative
performance work; these limits are part of the method contract.
