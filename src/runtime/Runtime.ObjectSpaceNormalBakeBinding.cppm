module;

#include <cstdint>
#include <string>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    enum class RuntimeObjectSpaceNormalBakeBindingStatus : std::uint8_t
    {
        Bound,
        WaitingForGpuTexture,
        InvalidContext,
        InvalidStableEntity,
        InvalidCompletion,
        StaleCompletion,
        StaleScene,
        StaleGeometry,
        StaleProgressiveState,
    };

    struct RuntimeObjectSpaceNormalBakeBindingContext
    {
        RuntimeObjectSpaceNormalBakeQueue* Queue = nullptr;
        RenderExtractionCache* Extraction = nullptr;
        const Graphics::GpuAssetCache* GpuAssets = nullptr;
        const Graphics::GpuWorld* GpuWorld = nullptr;
        ECS::Scene::Registry* Scene = nullptr;
        WorldHandle World{};
        std::uint64_t BindingEpoch = 0u;
    };

    struct RuntimeObjectSpaceNormalBakeCompletion
    {
        RuntimeObjectSpaceNormalBakeStaleKey StaleKey{};
        // Borrowed only for the duration of TryBindReadyObjectSpaceNormalBake.
        // The service retains the authoritative identity while a waiter is
        // eligible for completion, avoiding a potentially large byte copy.
        const RuntimeObjectSpaceNormalBakeIdentity* Identity = nullptr;
        Assets::AssetId GeneratedTextureAsset{};
        std::uint64_t CacheGeneration = 0u;
        std::uint64_t GeometryContentRevision = 0u;
        RuntimeObjectSpaceNormalBakeAssetSelection AssetSelection{
            RuntimeObjectSpaceNormalBakeAssetSelection::None};
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
            const RuntimeObjectSpaceNormalBakeBindingContext& context,
            const RuntimeObjectSpaceNormalBakeCompletion& completion);
}
