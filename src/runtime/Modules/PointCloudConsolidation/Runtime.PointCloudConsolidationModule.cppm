// Runtime consolidation requests, completion diagnostics and publication lifecycle.
module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.PointCloudConsolidationModule;
export import Extrinsic.Runtime.PointCloudConsolidationTypes;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.WorldHandle;

// Borrowed services retain their owners' C++ linkage without importing implementations.
extern "C++"
{
    namespace Extrinsic::RHI { class IDevice; }
    namespace Extrinsic::Runtime
    {
        class WorldRegistry;
        class SpatialIndexCache;
        class StepRunsService;
    }
}

export namespace Extrinsic::Runtime
{
    class PointCloudConsolidationGpuState;
    struct PointCloudConsolidationReadinessState;
    struct PointCloudConsolidationRunTable;
    struct PointCloudConsolidationRunContext;

    [[nodiscard]] PointCloudConsolidationAvailability
    ResolvePointCloudConsolidationAvailability(
        const GeometryEntityAvailability& availability,
        const PointCloudConsolidationPropertyRefs& properties,
        const PointCloudConsolidationConfig& config);


    extern "C++" class PointCloudConsolidationModule final : public IRuntimeModule
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
        [[nodiscard]] PointCloudConsolidationRunContext RunContext() noexcept;

        PointCloudConsolidationService m_Service{};
        std::unique_ptr<PointCloudConsolidationReadinessState> m_Readiness{};
        // The CPU and hybrid runs in progress (every backend runs as a step run of m_StepRuns).
        std::unique_ptr<PointCloudConsolidationRunTable> m_Runs{};
        StepRunsService* m_StepRuns{};
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
