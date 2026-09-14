module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ClusteringModule;
export import Extrinsic.Runtime.ClusteringTypes;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    class ClusteringGpuState;

    export class ClusteringService
    {
    public:
        ClusteringService() = default;
        ClusteringService(const ClusteringService&) = delete;
        ClusteringService& operator=(const ClusteringService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] CommandCorrelationId RunKMeans(RunKMeans command);

        [[nodiscard]] KernelEventSubscription SubscribeRunCompleted(
            std::function<void(const KMeansRunCompleted&)> listener);
        [[nodiscard]] KernelEventSubscription SubscribeClusterLabelsChanged(
            std::function<void(const ClusterLabelsChanged&)> listener);
        void Unsubscribe(KernelEventSubscription subscription);

        [[nodiscard]] ClusteringModuleStats Stats() const noexcept;

    private:
        friend class ClusteringModule;

        void Bind(CommandBus* commands,
                  KernelEventBus* events,
                  const ClusteringModuleStats* stats) noexcept;

        CommandBus* m_Commands{};
        KernelEventBus* m_Events{};
        const ClusteringModuleStats* m_Stats{};
    };

    export class ClusteringModule final : public IRuntimeModule
    {
    public:
        ClusteringModule();
        ~ClusteringModule() override;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] const ClusteringModuleStats& Stats() const noexcept
        {
            return m_Stats;
        }

    private:
        ClusteringService m_Service{};
        KernelEventSubscription m_JobCompletedSubscription{};
        KernelEventSubscription m_ClusterLabelsChangedSubscription{};
        KernelEventBus* m_Events{};
        JobService* m_Jobs{};
        WorldRegistry* m_Worlds{};
        EditorCommandHistory* m_History{};
        RHI::IDevice* m_Device{};
        std::unique_ptr<ClusteringGpuState> m_GpuState{};
        GpuQueueParticipantHandle m_GpuParticipant{};
        ClusteringModuleStats m_Stats{};
    };
}
