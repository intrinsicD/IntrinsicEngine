// One readback of a GPU property ring front for Accept (ADR 0030 decision 6), shared by the
// scalar transaction and the positions Accept. Include after the EditorProcessing,
// GpuPropertyBinding, SpatialIndexCache and RHI.Device imports; compiled in
// `Runtime.GeometryProcessingOperations.GpuPositions.cpp`.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    struct GpuFrontReadback
    {
        std::vector<std::byte> Bytes{}; // the front's bytes once Done and not Failed
        bool Done{}, Failed{};
        bool Abandoned{}; // set by the owner: a framed recorder then records nothing
        // The front stays leased until the bytes landed, whatever happens to the owner.
        std::optional<Graphics::GpuPropertyView> Lease{};
        std::shared_ptr<SpatialGpuResult> Framed{}; // the frame fallback's result, if used
    };

    // Queues one readback of `key`'s front in its own bytes: at once where the device can
    // (GRAPHICS-150), otherwise with the frame through the spatial index service. The
    // readback is a completion of the front's slot. False, queuing nothing, without a
    // resident front, a device or the service.
    [[nodiscard]] bool BeginGpuFrontReadback(
        const EditorProcessingContext&, Graphics::GpuPropertyResidency&, const Graphics::GpuPropertyKey&,
        const std::shared_ptr<GpuFrontReadback>&);
    // Folds a framed result in; true once the readback is done or failed.
    [[nodiscard]] bool PollGpuFrontReadback(GpuFrontReadback&);
}
}
