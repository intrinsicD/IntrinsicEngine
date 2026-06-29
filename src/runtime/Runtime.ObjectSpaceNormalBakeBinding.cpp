module;

#include <cstdint>
#include <string>
#include <utility>

module Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] RuntimeObjectSpaceNormalBakeBindingResult BindingFail(
            const RuntimeObjectSpaceNormalBakeBindingStatus status,
            std::string diagnostic,
            RuntimeObjectSpaceNormalBakeResult completion = {})
        {
            return RuntimeObjectSpaceNormalBakeBindingResult{
                .Status = status,
                .Completion = std::move(completion),
                .Diagnostic = std::move(diagnostic),
            };
        }
    }

    const char* DebugNameForRuntimeObjectSpaceNormalBakeBindingStatus(
        const RuntimeObjectSpaceNormalBakeBindingStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeObjectSpaceNormalBakeBindingStatus::Bound:
            return "Bound";
        case RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture:
            return "WaitingForGpuTexture";
        case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity:
            return "InvalidStableEntity";
        case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion:
            return "InvalidCompletion";
        case RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion:
            return "StaleCompletion";
        }
        return "Unknown";
    }

    RuntimeObjectSpaceNormalBakeBindingResult TryBindReadyObjectSpaceNormalBake(
        RuntimeObjectSpaceNormalBakeQueue& queue,
        RenderExtractionCache& extraction,
        const Graphics::GpuAssetCache& gpuAssets,
        const std::uint32_t stableEntityId,
        const RuntimeObjectSpaceNormalBakeStaleKey& completion)
    {
        if (stableEntityId == 0u)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity,
                "object-space normal bake binding has no stable render entity");
        }

        const Assets::AssetId generated = completion.Bake.GeneratedTextureAsset;
        if (!generated.IsValid() || completion.Bake.Source.EntityKey == 0u)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion,
                "object-space normal bake completion is missing generated texture or entity key");
        }

        if (completion.Bake.Source.EntityKey !=
            static_cast<std::uint64_t>(stableEntityId))
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity,
                "object-space normal bake completion entity does not match target stable entity");
        }

        const auto view = gpuAssets.GetView(generated);
        if (!view.has_value() || view->Kind != Graphics::GpuAssetKind::Texture)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture,
                "object-space normal bake texture is not ready in GpuAssetCache");
        }

        RuntimeObjectSpaceNormalBakeResult ready = queue.Complete(completion);
        if (!ready.Succeeded())
        {
            const RuntimeObjectSpaceNormalBakeBindingStatus status =
                ready.Status == RuntimeObjectSpaceNormalBakeStatus::StaleCompletion
                    ? RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion
                    : RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion;
            return BindingFail(status, ready.Diagnostic, std::move(ready));
        }

        extraction.SetMaterialTextureAssetBindings(
            stableEntityId,
            Graphics::MaterialTextureAssetBindings{
                .Albedo = {},
                .Normal = ready.Submission.GeneratedTextureAsset,
                .MetallicRoughness = {},
                .Emissive = {},
                .NormalSpace =
                    Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal,
            });

        return RuntimeObjectSpaceNormalBakeBindingResult{
            .Status = RuntimeObjectSpaceNormalBakeBindingStatus::Bound,
            .Completion = std::move(ready),
            .BoundNormalTexture = generated,
            .Diagnostic = "object-space normal bake material binding installed",
        };
    }
}
