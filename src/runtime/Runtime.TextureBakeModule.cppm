module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.TextureBakeModule;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    export struct TextureBakeProducerContext
    {
        RuntimeObjectSpaceNormalBakeQueue* Queue{};
        WorldHandle World{};
        std::uint64_t BindingEpoch{0u};
        const RHI::IDevice* Device{};
        // Main-thread deferred producers lock this token immediately before
        // dereferencing Queue. Target changes and shutdown invalidate it.
        std::weak_ptr<void> Lifetime{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Queue != nullptr &&
                   World.IsValid() &&
                   BindingEpoch != 0u &&
                   Device != nullptr &&
                   !Lifetime.expired();
        }
    };

    export struct TextureBakeBindingChanged
    {
        WorldHandle World{};
        std::uint64_t BindingEpoch{0u};
    };

    export struct TextureBakeModuleStats
    {
        std::uint64_t BakeRequests{0u};
        std::uint64_t BakeRequestsAccepted{0u};
        std::uint64_t BakeRequestsRejected{0u};
        std::uint64_t BindingChanges{0u};
    };

    export enum class TextureBakeMutationStatus : std::uint8_t
    {
        Success,
        MissingScene,
        StaleEntity,
        MissingTexture,
        DuplicateName,
        InvalidName,
        IncompatibleConsumer,
        AssetDestroyFailed,
        CommandFailed,
    };

    export struct TextureBakeMutationResult
    {
        TextureBakeMutationStatus Status{TextureBakeMutationStatus::Success};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == TextureBakeMutationStatus::Success;
        }
    };

    export struct TextureBakeConsumerUpdateRequest
    {
        std::uint32_t StableEntityId{0u};
        std::string OutputName{};
        std::vector<BakedPropertyTextureConsumer> Consumers{};
    };

    export struct TextureBakeSnapshot
    {
        std::vector<BakedPropertyTextureRecord> Textures{};
        bool GpuOperational{false};
        std::string Diagnostic{};
    };

    export class TextureBakeService
    {
    public:
        TextureBakeService();
        ~TextureBakeService();

        TextureBakeService(const TextureBakeService&) = delete;
        TextureBakeService& operator=(const TextureBakeService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] SelectedMeshTextureBakeResult Bake(
            const SelectedMeshTextureBakeRequest& request);
        [[nodiscard]] TextureBakeProducerContext ProducerContext() const noexcept;
        [[nodiscard]] TextureBakeModuleStats Stats() const noexcept;
        [[nodiscard]] TextureBakeSnapshot Snapshot(
            std::uint32_t stableEntityId) const;
        [[nodiscard]] TextureBakeMutationResult Rename(
            std::uint32_t stableEntityId,
            std::string_view currentName,
            std::string_view newName);
        [[nodiscard]] TextureBakeMutationResult Remove(
            std::uint32_t stableEntityId,
            std::string_view outputName);
        [[nodiscard]] TextureBakeMutationResult SetConsumers(
            const TextureBakeConsumerUpdateRequest& request);

    private:
        friend class TextureBakeModule;

        void Bind(
            SelectedMeshTextureBakeContext context,
            RuntimeObjectSpaceNormalBakeQueue* queue,
            RHI::IDevice* device,
            Graphics::GpuAssetCache* gpuAssets,
            Graphics::IRenderer* renderer,
            RenderExtractionCache* extraction,
            TextureBakeModuleStats* stats) noexcept;
        void SetTarget(WorldHandle world,
                       std::uint64_t bindingEpoch,
                       ECS::Scene::Registry* scene) noexcept;
        void SetCommandHistory(EditorCommandHistory* history) noexcept;
        void DetachTargets(WorldHandle world,
                           std::uint64_t bindingEpoch,
                           bool destroyGeneratedAssets) noexcept;
        void DestroySceneAssets(ECS::Scene::Registry& scene) noexcept;
        void RestoreReadyBindings() noexcept;
        [[nodiscard]] GpuQueueParticipantHandle
            RegisterGpuQueueParticipant(JobService& jobs);
        void Unbind() noexcept;

        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };

    export class TextureBakeModule final : public IRuntimeModule
    {
    public:
        TextureBakeModule();
        ~TextureBakeModule() override;

        TextureBakeModule(const TextureBakeModule&) = delete;
        TextureBakeModule& operator=(const TextureBakeModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
