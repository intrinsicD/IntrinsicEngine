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
import Extrinsic.Runtime.WorldHandle;

// Borrowed services retain their owners' C++ linkage without importing implementations.
extern "C++"
{
    namespace Extrinsic::RHI { class IDevice; }
    namespace Extrinsic::Runtime { class WorldRegistry; class SpatialIndexCache; class EditorWorkspaceSlot; }
}

namespace Extrinsic::Runtime
{
    class ClusteringGpuState;
    struct KMeansCaptureCache;

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
            WorldHandle World{}; // the world it was admitted in; it starts only there
            bool Seeding{false}; // waits for its seeds; its capture was counted when it first drained
        };
        void StartHeld();
        void CancelHeld(KMeansRunStatus status, std::string why);
        void ReleaseCapture() noexcept;

        ClusteringService m_Service{};
        std::optional<HeldRequest> m_Held{};
        KernelEventSubscription m_JobCompletedSubscription{};
        KernelEventSubscription m_ClusterLabelsChangedSubscription{};
        KernelEventSubscription m_ActiveWorldChangedSubscription{};
        KernelEventBus* m_Events{};
        JobService* m_Jobs{};
        WorldRegistry* m_Worlds{};
        EditorCommandHistory* m_History{};
        RHI::IDevice* m_Device{};
        SpatialIndexCache* m_SpatialIndices{};
        // A GPU run's holder and Accept are editor jobs of this workspace: the app's while it lends one (through the
        // service), else the module's own.
        std::unique_ptr<EditorWorkspaceSlot> m_Workspace;
        std::unique_ptr<ClusteringGpuState> m_GpuState{};
        std::unique_ptr<KMeansCaptureCache> m_Captures; // the last request's capture, shared by its cloud revision
        GpuQueueParticipantHandle m_GpuParticipant{};
        ClusteringModuleStats m_Stats{};
    };
}
