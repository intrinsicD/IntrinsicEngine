// Clustering module lifecycle and private execution state; callers use ClusteringTypes.
module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ClusteringModule;
export import Extrinsic.Runtime.ClusteringTypes;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.WorldHandle;

// Borrowed services retain their owners' C++ linkage without importing implementations.
extern "C++"
{
    namespace Extrinsic::RHI { class IDevice; }
    namespace Extrinsic::Runtime { class WorldRegistry; class SpatialIndexCache; }
}

namespace Extrinsic::Runtime
{
    class ClusteringGpuState;

    export extern "C++" class ClusteringModule final : public IRuntimeModule
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
        // A replacing GPU request waits here, newest only, until the run it replaces settled.
        struct HeldRequest
        {
            RunKMeans Command{};
            CommandCorrelationId Correlation{};
        };
        void HoldReplacement(RunKMeans command, CommandCorrelationId correlation);
        void StartHeld();
        void CancelHeld(std::string why);

        ClusteringService m_Service{};
        std::optional<HeldRequest> m_Held{};
        CommandBus* m_Commands{};
        KernelEventSubscription m_JobCompletedSubscription{};
        KernelEventSubscription m_ClusterLabelsChangedSubscription{};
        KernelEventBus* m_Events{};
        JobService* m_Jobs{};
        WorldRegistry* m_Worlds{};
        EditorCommandHistory* m_History{};
        RHI::IDevice* m_Device{};
        SpatialIndexCache* m_SpatialIndices{};
        // A GPU run is a step run (its controls and observation) whose holder and Accept are editor jobs of the
        // service's workspace: the app's while it lends one, else the module's own.
        StepRunsService* m_Steps{};
        std::unique_ptr<ClusteringGpuState> m_GpuState{};
        GpuQueueParticipantHandle m_GpuParticipant{};
        ClusteringModuleStats m_Stats{};
    };
}
