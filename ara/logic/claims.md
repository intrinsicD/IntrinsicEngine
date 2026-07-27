# Claims

## C01: PR-fast ccache provides a material same-shape latency reduction
- **Statement**: For the CI-007 `pr-fast` gate shape at commit `5394e51b`, five
  warm ccache samples reduced build median/p95 by 56.7%/58.3% and total
  median/p95 by 47.6%/50.0% versus five contemporary cold samples, while every
  gate and hermetic parity probe passed with zero ccache errors.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A same-shape repeat cohort reports no warm median
  build improvement, a regressed warm build p95, nonzero ccache errors, or a
  cached/clean correctness mismatch.
- **Proof**: [ara/evidence/tables/ci007_ccache_cohort.md,
  tasks/archive/CI-007-module-safe-persistent-ccache-pilot.md,
  docs/benchmarking/ci-policy.md]
- **Dependencies**: []
- **Tags**: CI, ccache, C++23 modules, gate latency
- **From staging**: O15

## C02: Bounded disk BFF supports three deterministic CPU reference controls
- **Statement**: On supported triangle disks, the METHOD-023 CPU reference
  deterministically produces finite UVs for automatic conformal, approximate
  target-boundary-length, and prescribed exterior-angle controls, reports
  residual diagnostics, and fails closed for unsupported or invalid input.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A supported deterministic fixture produces
  non-finite or nondeterministic UVs, a declared control fails its residual or
  orientation contract, or unsupported topology/input returns a UV payload.
- **Proof**: [tasks/done/METHOD-023-boundary-first-flattening-reference-backend.md,
  tests/unit/geometry/Test.BoundaryFirstFlattening.cpp,
  src/geometry/Geometry.Parameterization.Bff.cpp,
  commit 4bf4f67b]
- **Dependencies**: [K14, K15]
- **Tags**: geometry, parameterization, BFF, CPU reference
- **From staging**: O30

## C03: Adaptive Delaunay/QEF meshing is a gated research hypothesis
- **Statement**: QEF-derived feature samples may improve an adaptive Delaunay
  implicit-meshing reference on thin and sharp-feature fixtures, but the broad
  combination is prior-art constrained and does not warrant a 3D engine
  implementation unless a deterministic adversarial 2D falsifier passes first.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The 2D adversarial study fails its manifoldness,
  termination, feature-retention, or field-evaluation gates, or prior-art
  comparison leaves no distinct evidence question worth testing.
- **Proof**: [N220,
  tasks/backlog/methods/METHOD-027-adaptive-delaunay-qef-implicit-meshing.md]
- **Dependencies**: []
- **Tags**: geometry, implicit meshing, Delaunay, QEF, falsifier
- **From staging**: O39

## C04: Confidence-driven subdivision may improve guided Walk on Stars
- **Statement**: After a canonical METHOD-004 CPU reference exists, spatial
  subdivision driven by contribution and direction confidence may improve the
  variance-memory frontier of guided Walk on Stars, as a provisional transfer
  rather than a claim of a new guiding family.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Matched deterministic fixtures show no variance
  benefit at equal sample and memory budgets, confidence adds unacceptable
  bias, or the measured behavior is already fully explained by cited guided-WoSt
  prior art.
- **Proof**: [N220,
  tasks/backlog/methods/METHOD-028-confidence-driven-walk-on-stars-guiding.md]
- **Dependencies**: []
- **Tags**: Monte Carlo PDE, Walk on Stars, guiding, confidence, memory
- **From staging**: O40

## C05: Invariant-aware mip objectives may preserve scientific fields better
- **Statement**: At equal storage, field-specific optimization objectives may
  reduce angular error, categorical bleed, or isoline drift in normal, label,
  and signed-scalar mip pyramids relative to raw-channel averaging.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Deterministic CPU fixtures show no material
  improvement on the declared invariant metrics, introduce unacceptable
  reconstruction artifacts, or exceed the task's storage and build-cost limits.
- **Proof**: [N220,
  tasks/backlog/geometry/GEOM-065-invariant-aware-scientific-field-mip-pyramids.md]
- **Dependencies**: []
- **Tags**: geometry, scientific fields, mipmaps, invariants, visualization
- **From staging**: O45

## C06: Runtime background work has one CPU-supported lifecycle
- **Statement**: In the default CPU-supported runtime contract, production
  asynchronous workflows use the kernel-owned `JobService`; Engine applies at
  most eight completions before event pump B, `AsyncWorkModule` publishes and
  withdraws that borrowed service and cancels shutdown survivors, and the
  superseded `StreamingExecutor` and `DerivedJobGraph` modules are absent from
  source, CMake registration, dedicated tests, and the generated inventory.
- **Status**: supported — CPU-supported runtime scope; no new GPU/Vulkan
  execution claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A production workflow imports or submits through
  either retired surface, either module returns to source/CMake/inventory, the
  frame path performs an unbounded completion drain, or cancellation/staleness
  permits a result to publish more than once.
- **Proof**: [tasks/done/RUNTIME-194-consolidate-runtime-work-execution.md,
  src/runtime/Runtime.AsyncWorkModule.cpp,
  src/runtime/Runtime.Engine.cpp,
  tests/contract/runtime/Test.RuntimeJobService.cpp,
  tests/contract/runtime/Test.AsyncWorkModule.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/contract/runtime/Test.ImGuiAdapterEngineWiring.cpp,
  docs/api/generated/module_inventory.md]
- **Dependencies**: [K05]
- **Tags**: runtime, JobService, CPU-supported, lifecycle, retirement
- **From staging**: O61

## C07: Multi-range GPU-result readback has a CPU-supported transport contract
- **Statement**: In CPU/fake-queue contracts, `Graphics.GpuTransfer` represents
  one logical GPU result as ordered copied ranges, exposes those bytes only
  after exact generational-handle, descriptor, access, range, and byte-count
  revalidation, and composes with canonical `JobService` parked publication,
  dependency release, world cancellation, and exactly-once consumption.
- **Status**: supported — CPU/fake-queue contract only; operational production
  evidence is tracked separately by C08 and C09
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A malformed or stale request exposes bytes, a
  cancelled in-flight sink loses required storage or remains consumable, a
  batch consumes twice, a dependent releases before successful publication, or
  world cancellation permits later publication.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/graphics/renderer/Graphics.GpuTransfer.cppm,
  src/graphics/renderer/Graphics.GpuTransfer.cpp,
  tests/contract/graphics/Test.GpuTransferFacade.cpp,
  tests/contract/runtime/Test.GpuResultReadbackJob.cpp]
- **Dependencies**: [C06]
- **Tags**: graphics, runtime, GPU readback, JobService, CPU-supported
- **From staging**: O62

## C08: K-Means uses the shared result batch on operational Vulkan
- **Statement**: The production K-Means GPU backend submits labels, squared
  distances, and centroids through one copied `Graphics.GpuTransfer` batch,
  deduplicates their common source to one transfer-read barrier, consumes the
  batch exactly once in its typed adapter, and matches its CPU reference on the
  deterministic separated-clusters Vulkan fixture.
- **Status**: supported — ASan+UBSan promoted Vulkan on NVIDIA GeForce RTX 3050,
  driver 590.48.01; no Progressive Poisson or whole-task claim
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: K-Means imports or constructs the retired async
  wrapper, submits more than one logical result batch, emits more than one
  transfer-read barrier for its shared Work buffer, consumes the result more
  than once, or the operational parity fixture disagrees with the CPU reference.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/runtime/Runtime.KMeansGpuBackend.cppm,
  src/runtime/Runtime.KMeansGpuBackend.cpp,
  src/runtime/Runtime.KMeansGpuJobQueue.cpp,
  tests/contract/runtime/Test.KMeansGpuBackend.cpp,
  tests/integration/runtime/Test.KMeansGpuBackendGpuSmoke.cpp]
- **Dependencies**: [C07]
- **Tags**: K-Means, graphics, runtime, GPU readback, Vulkan, parity
- **From staging**: O63

## C09: Progressive Poisson result transport operates on Vulkan
- **Statement**: The Progressive Poisson GPU backend exposes its
  `AcceptedKeys`, `LevelOffsets`, and `SplatRadii` production buffers to one
  copied `Graphics.GpuTransfer` batch, consumes that batch exactly once in its
  typed adapter, and allocates or records no duplicate result buffers/copies or
  blocking `IDevice::ReadBuffer` result path. An actual Vulkan smoke transports
  and parses a CPU-reference-shaped payload through those three buffers.
- **Status**: supported — ASan+UBSan promoted Vulkan transport/parser evidence
  on NVIDIA GeForce RTX 3050, driver 590.48.01; the payload is seeded and this
  is not Progressive Poisson compute or public CPU/GPU parity
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The backend reintroduces duplicate readback
  targets or result pre-copies, calls blocking `IDevice::ReadBuffer` for its
  result, submits more than one logical batch, consumes the result more than
  once, or the actual-Vulkan transport fixture does not reproduce the seeded
  CPU-reference-shaped order, level offsets, and splat radii.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/runtime/Runtime.ProgressivePoissonGpuBackend.cppm,
  src/runtime/Runtime.ProgressivePoissonGpuBackend.cpp,
  tests/contract/runtime/Test.ProgressivePoissonGpuBackend.cpp,
  tests/integration/runtime/Test.ProgressivePoissonGpuResultReadbackGpuSmoke.cpp]
- **Dependencies**: [C07]
- **Tags**: Progressive Poisson, graphics, runtime, GPU readback, Vulkan,
  transport
- **From staging**: O64

## C10: Runtime compute-result readback has one shared lifecycle
- **Statement**: Every censused runtime compute-result workflow uses one
  copied, multi-range `Graphics.GpuTransfer` batch with feature-owned typed
  parsing and canonical `JobService` waiting/publication. The superseded
  `Runtime.AsyncBufferReadback` and `Runtime.GpuReadbackJob` modules, build
  entries, and wrapper-only tests are absent. Renderer `SelectionReadback`
  remains an explicitly separate frame-correlated lifecycle.
- **Status**: supported — CPU/fake-queue contracts plus ASan+UBSan promoted
  Vulkan on NVIDIA GeForce RTX 3050, driver 590.48.01; Progressive Poisson
  compute/public parity remains excluded and owned by METHOD-014
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A runtime compute-result producer bypasses the
  shared transfer batch, reintroduces either retired module or a feature-named
  result queue/service, performs a blocking result `IDevice::ReadBuffer`, lets
  the transport interpret feature semantics, or folds renderer selection into
  this lifecycle.
- **Proof**: [tasks/done/RUNTIME-195-unified-gpu-result-readback.md,
  src/graphics/renderer/Graphics.GpuTransfer.cppm,
  src/graphics/renderer/Graphics.GpuTransfer.cpp,
  src/runtime/Runtime.KMeansGpuBackend.cpp,
  src/runtime/Runtime.ProgressivePoissonGpuBackend.cpp,
  tests/contract/runtime/Test.GpuResultReadbackJob.cpp,
  tests/contract/runtime/Test.RuntimeEngineLayering.cpp,
  tests/integration/runtime/Test.GpuResultReadbackGpuSmoke.cpp,
  docs/api/generated/module_inventory.md]
- **Dependencies**: [C06, C08, C09]
- **Tags**: graphics, runtime, GPU readback, JobService, Vulkan, retirement
- **From staging**: O65
