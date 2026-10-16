# Graphics

Graphics consumes snapshots and geometry views. Runtime owns live ECS and asset
service integration; backend types do not cross the public RHI boundary.

- [`rhi`](rhi/README.md): device, command and resource contracts.
- [`assets`](assets/README.md): GPU asset state and uploads keyed by asset IDs.
- [`vulkan`](vulkan/README.md): the Vulkan implementation and private backend state.
- [`framegraph`](framegraph/README.md): recipe compilation, resource lifetimes
  and synchronization.
- [`renderer`](renderer/README.md): render preparation, recipe execution and
  the headless Null backend.

Start with the [graphics architecture](../../docs/architecture/graphics.md) and
[layering contract](../../docs/architecture/layering.md). CPU contract tests and
opt-in Vulkan tests provide separate evidence; see the
[test strategy](../../docs/architecture/test-strategy.md).
