module;

#include <cstdint>
#include <string>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;

export namespace Extrinsic::Runtime
{
    enum class RuntimeObjectSpaceNormalBakeBindingStatus : std::uint8_t
    {
        Bound,
        WaitingForGpuTexture,
        InvalidStableEntity,
        InvalidCompletion,
        StaleCompletion,
    };

    struct RuntimeObjectSpaceNormalBakeBindingResult
    {
        RuntimeObjectSpaceNormalBakeBindingStatus Status{
            RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion};
        RuntimeObjectSpaceNormalBakeResult Completion{};
        Assets::AssetId BoundNormalTexture{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeObjectSpaceNormalBakeBindingStatus::Bound;
        }
    };

    [[nodiscard]] const char* DebugNameForRuntimeObjectSpaceNormalBakeBindingStatus(
        RuntimeObjectSpaceNormalBakeBindingStatus status) noexcept;

    [[nodiscard]] RuntimeObjectSpaceNormalBakeBindingResult
        TryBindReadyObjectSpaceNormalBake(
            RuntimeObjectSpaceNormalBakeQueue& queue,
            RenderExtractionCache& extraction,
            const Graphics::GpuAssetCache& gpuAssets,
            std::uint32_t stableEntityId,
            const RuntimeObjectSpaceNormalBakeStaleKey& completion);
}
