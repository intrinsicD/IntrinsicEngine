# Rigid ICP integration

The engine retains its existing point-to-point rigid rotation/translation solve
and linearized point-to-plane 6-by-6 normal-equation solve. This integration
changes operand binding and nearest-correspondence providers. It does not add a
registration formulation.

## Formulation and literature intake

- Besl and McKay, 1992, [A Method for Registration of 3-D Shapes](https://doi.org/10.1109/34.121791): nearest correspondences and alternating rigid alignment are the baseline.
- Chen and Medioni, 1992, [Object Modelling by Registration of Multiple Range Images](https://doi.org/10.1016/0262-8856(92)90066-C): target normals define the point-to-plane objective. Source normals cannot substitute for target normals.
- Rusinkiewicz and Levoy, 2001, [Efficient Variants of the ICP Algorithm](https://www.cs.princeton.edu/~smr/papers/fasticp/): correspondence search is a distinct axis from sampling, rejection and minimization. This is the boundary used here.
- Segal et al., 2009, [Generalized ICP](https://www.robots.ox.ac.uk/~avsegal/generalized_icp.html): anisotropic costs require additional covariance semantics. No GICP option is exposed.
- Chetverikov et al., 2002, [The Trimmed Iterative Closest Point Algorithm](https://doi.org/10.1109/ICPR.2002.1047997): the current fixed inlier fraction is retained; this integration does not implement overlap estimation or claim full Trimmed ICP equivalence.
- Zhang, Yao and Deng, 2021, [Fast and Robust ICP](https://arxiv.org/abs/2007.07627v3): Anderson-accelerated majorization-minimization and its robust objective are separate algorithms. Existing optional residual weights do not establish equivalence to that method.

The 2026-09-08 review retrieved the original author pages for Efficient ICP and
GICP and the Fast and Robust ICP abstract. Downloads of the Besl/McKay and
Chen/Medioni PDFs timed out. Their stable citations and the existing engine
formulations are retained; no new derivation depends on an unread source.

## API and numerical contract

`Geometry.Registration::AlignICP` remains the CPU KD-tree reference.
`AlignICPWithQueries` accepts a batch nearest-index callback. `MakeICPQueries`
and `AdvanceICP` let an asynchronous owner suspend between correspondence
batches while using the same rejection, weighting, solve and convergence code.
Indices address compact target arrays; `UINT32_MAX` means no match. Callback
failure and out-of-range results fail closed. Query positions use float3;
residual accumulation and the transform solve use double precision.

The runtime compacts live property rows without changing geometry. Target
normal compaction uses the same domain/deletion mask. Normals transform by the
inverse transpose and must be finite, nonzero and count-matched. Only the source
entity transform is published through editor history. See the
[workflow and ownership contract](../../../docs/architecture/registration.md).

## Spatial acceleration and scope

CPU LBVH uses an immutable lease from `Runtime.SpatialIndexCache`. Vulkan uses
its framed, reusable nearest-query batch and original-slot mapping. The target
index persists across iterations and registrations until the bound property,
deletion mask, identity or indexed transform changes. CPU KD-tree remains the
default. GPU timings must include frame scheduling, transfers and CPU solve;
kernel dispatch time alone cannot justify a default change.

The [CPU smoke manifest](../../../benchmarks/geometry/manifests/registration_spatial_smoke.yaml)
compares complete KD-tree runs, LBVH build plus run, and LBVH runs with an existing
index. The framed GPU integration test records equivalent end-to-end timings.
Local dirty-source smoke timings are diagnostic and are not performance claims.
