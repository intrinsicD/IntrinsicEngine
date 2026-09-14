// Runtime consolidation requests, completion diagnostics and publication lifecycle.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.PointCloudConsolidationModule;
export import Extrinsic.Runtime.PointCloudConsolidationTypes;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.SpatialIndexCache;
import Geometry.PointCloud.Consolidation;

export namespace Extrinsic::Runtime
{
    class PointCloudConsolidationGpuState;

    [[nodiscard]] bool IsValidPointCloudConsolidationPropertyRefs(
        const PointCloudConsolidationPropertyRefs& properties) noexcept;

    [[nodiscard]] PointCloudConsolidationAvailability
    ResolvePointCloudConsolidationAvailability(
        const GeometryEntityAvailability& availability,
        const PointCloudConsolidationPropertyRefs& properties,
        const PointCloudConsolidationConfig& config);

    class PointCloudConsolidationService
    {
    public:
        PointCloudConsolidationService() = default;
        PointCloudConsolidationService(
            const PointCloudConsolidationService&) = delete;
        PointCloudConsolidationService& operator=(
            const PointCloudConsolidationService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] CommandCorrelationId Run(
            PointCloudConsolidationRequest request);
        [[nodiscard]] KernelEventSubscription SubscribeCompleted(
            std::function<void(const PointCloudConsolidationResult&)> listener);
        void Unsubscribe(KernelEventSubscription subscription);
        [[nodiscard]] PointCloudConsolidationModuleStats Stats() const noexcept;

    private:
        friend class PointCloudConsolidationModule;

        void Bind(
            CommandBus* commands,
            KernelEventBus* events,
            const PointCloudConsolidationModuleStats* stats) noexcept;

        CommandBus* m_Commands{};
        KernelEventBus* m_Events{};
        const PointCloudConsolidationModuleStats* m_Stats{};
    };

    class PointCloudConsolidationModule final : public IRuntimeModule
    {
    public:
        PointCloudConsolidationModule();
        ~PointCloudConsolidationModule() override;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] const PointCloudConsolidationModuleStats& Stats()
            const noexcept
        {
            return m_Stats;
        }

    private:
        PointCloudConsolidationService m_Service{};
        KernelEventSubscription m_JobCompletedSubscription{};
        KernelEventBus* m_Events{};
        JobService* m_Jobs{};
        WorldRegistry* m_Worlds{};
        EditorCommandHistory* m_History{};
        SpatialIndexCache* m_SpatialIndices{};
        RHI::IDevice* m_Device{};
        std::unique_ptr<PointCloudConsolidationGpuState> m_GpuState{};
        GpuQueueParticipantHandle m_GpuParticipant{};
        PointCloudConsolidationModuleStats m_Stats{};
    };
}
