# Graphics/RHI

This directory contains the `RHI` module/files.

## Contents

- `CMakeLists.txt`
- `RHI.CommandContext.cppm`
- `RHI.Device.cppm`
- `RHI.FrameHandle.cppm`

## Transfer uploads

- `RHI.TransferQueue.cppm` declares `ITransferQueue`, the async upload seam used
  by runtime/streaming paths. Buffer uploads and single-subresource texture
  uploads remain non-blocking. `UploadTextureFullChain(TextureHandle,
  std::span<const std::byte>)` is the GRAPHICS-018T seam for packed full-chain
  texture uploads; callers must pack bytes according to
  `ComputeFullChainUploadLayout()` and backends fail closed with an invalid
  `TransferToken` when metadata or byte counts are unsupported.
- `RHI.TextureUpload.cppm` owns backend-neutral texture upload math:
  per-format byte/block helpers, Vulkan-safe subresource offset alignment, and
  the layer-major / mip-minor `TextureUploadLayout` used by future batched
  transfer submissions.

