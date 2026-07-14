module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.SelectedEntityAnalysisModule;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    export enum class SelectedEntityAnalysisConsumer : std::uint8_t
    {
        Inspector,
        MeshDomainWindow,
        GraphDomainWindow,
        PointCloudDomainWindow,
    };

    export enum class SelectedEntityAnalysisState : std::uint8_t
    {
        Missing,
        Pending,
        Ready,
        Stale,
        Failed,
    };

    export struct SelectedEntityAnalysisKey
    {
        WorldHandle World{};
        std::uint32_t StableEntityId{0u};
        SelectedEntityAnalysisConsumer Consumer{
            SelectedEntityAnalysisConsumer::Inspector};
        std::uint64_t GeometryRevision{0u};
        std::uint64_t BindingGeneration{0u};
        std::uint64_t RequestIdentity{0u};

        friend bool operator==(const SelectedEntityAnalysisKey&,
                               const SelectedEntityAnalysisKey&) = default;
    };

    export struct SelectedEntityChannelAnalysis
    {
        bool HasBinding{false};
        VertexChannel Channel{VertexChannel::Normal};
        AttributeBindResult Resolver{};
    };

    export struct SelectedEntityUvAnalysis
    {
        bool IsMesh{false};
        bool HasTexcoords{false};
        bool TexcoordCountMatchesVertices{false};
        bool TexcoordsFinite{false};
        std::size_t VertexCount{0u};
        std::size_t TexcoordCount{0u};
        std::size_t ElementsScanned{0u};
    };

    export struct SelectedEntityAnalysisResult
    {
        SelectedEntityChannelAnalysis Normal{
            .Channel = VertexChannel::Normal};
        SelectedEntityChannelAnalysis Color{
            .Channel = VertexChannel::Color};
        SelectedEntityUvAnalysis Uv{};
        std::size_t ChannelElementsScanned{0u};
    };

    export struct SelectedEntityAnalysisView
    {
        SelectedEntityAnalysisState State{
            SelectedEntityAnalysisState::Missing};
        SelectedEntityAnalysisKey Key{};
        SelectedEntityAnalysisResult Result{};
        bool PreviousReadyRetained{false};
        std::string Diagnostic{};
    };

    export struct SelectedEntityAnalysisStats
    {
        std::uint64_t Requests{0u};
        std::uint64_t RequestDeduplications{0u};
        std::uint64_t ReadyCacheHits{0u};
        std::uint64_t CommandsHandled{0u};
        std::uint64_t SnapshotsCaptured{0u};
        std::uint64_t JobsSubmitted{0u};
        std::uint64_t JobSubmissionFailures{0u};
        std::uint64_t CompletionEvents{0u};
        std::uint64_t ResultsApplied{0u};
        std::uint64_t StaleResultsRejected{0u};
        std::uint64_t JobsCancelledForRevision{0u};
        std::uint64_t GeometryRevisionAdvances{0u};
        std::uint64_t DeferredApplyResults{0u};
        std::uint64_t LastUiBuildApplied{0u};
        std::uint64_t MaxUiBuildApplied{0u};
        std::uint64_t PendingApplyResults{0u};
    };

    export class SelectedEntityAnalysisModule;

    export class SelectedEntityAnalysisService
    {
    public:
        SelectedEntityAnalysisService() = default;
        SelectedEntityAnalysisService(
            const SelectedEntityAnalysisService&) = delete;
        SelectedEntityAnalysisService& operator=(
            const SelectedEntityAnalysisService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] SelectedEntityAnalysisView Request(
            WorldHandle world,
            std::uint32_t stableEntityId,
            SelectedEntityAnalysisConsumer consumer);
        [[nodiscard]] SelectedEntityAnalysisView Query(
            WorldHandle world,
            std::uint32_t stableEntityId,
            SelectedEntityAnalysisConsumer consumer) const;
        [[nodiscard]] SelectedEntityAnalysisStats Stats() const noexcept;

    private:
        friend class SelectedEntityAnalysisModule;
        void Bind(SelectedEntityAnalysisModule* module) noexcept;

        SelectedEntityAnalysisModule* m_Module{};
    };

    export class SelectedEntityAnalysisModule final : public IRuntimeModule
    {
    public:
        explicit SelectedEntityAnalysisModule(
            std::size_t uiBuildApplyBudget = 2u);
        ~SelectedEntityAnalysisModule() override;

        SelectedEntityAnalysisModule(
            const SelectedEntityAnalysisModule&) = delete;
        SelectedEntityAnalysisModule& operator=(
            const SelectedEntityAnalysisModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] SelectedEntityAnalysisStats Stats() const noexcept;

    private:
        friend class SelectedEntityAnalysisService;
        struct Impl;

        [[nodiscard]] SelectedEntityAnalysisView Request(
            WorldHandle world,
            std::uint32_t stableEntityId,
            SelectedEntityAnalysisConsumer consumer);
        [[nodiscard]] SelectedEntityAnalysisView Query(
            WorldHandle world,
            std::uint32_t stableEntityId,
            SelectedEntityAnalysisConsumer consumer) const;

        std::unique_ptr<Impl> m_Impl{};
        SelectedEntityAnalysisService m_Service{};
    };
}
