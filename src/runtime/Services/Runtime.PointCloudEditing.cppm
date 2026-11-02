// ARCH-022: point-cloud edits for plugins. Captures an entity's live points and normals, gives neighbourhood rows
// on the CPU or the GPU, and publishes new positions into an output property with an undo entry. It wraps the
// editor's shared point helpers, so a plugin imports no editor module, and it holds no plugin callable: undo
// entries hold host data, so they work after the plugin that recorded them is unloaded.
module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

export module Extrinsic.Runtime.PointCloudEditing;

export import Extrinsic.Runtime.GeometryAvailability;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Geometry.PointLBVH;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.ModuleLifecycle;

export namespace Extrinsic::Runtime
{
    // What to read and where to write. Unknown domains resolve to the entity's primary point domain, the normals'
    // and the output's to the positions'.
    struct PointEditRequest
    {
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Positions{};
        GeometryPropertyRef Normals{};
        GeometryPropertyRef Output{};
        std::size_t MinimumLiveCount{2u};
        bool CopyValues{true};    // false: readiness only, nothing copied
        bool AcquireIndex{false}; // the cached spatial index of the points (CPU and GPU neighbourhoods)
    };

    struct PointEdit; // a capture, owned by the service's code

    struct PointEditView
    {
        std::span<const glm::vec3> Points{};
        std::span<const glm::vec3> PointNormals{};
        std::size_t SlotCount{0u}, LiveCount{0u};
        bool ValidLbvh{false};  // the points fit an LBVH (count and coordinates)
        bool IndexReused{false};
        GeometryPropertyRef Output{}; // with its resolved domain
    };

    enum class PointRowsState : std::uint8_t { Pending, Ready, Failed };

    struct PointGpuRowStats
    {
        std::size_t QueryBatches{0u}, WorkspaceBuilds{0u};
        double Milliseconds{0.0};
    };

    enum class PointPublishStatus : std::uint8_t { Applied, Stale, Rejected };

    class PointCloudEditingService
    {
    public:
        PointCloudEditingService();
        ~PointCloudEditingService();
        PointCloudEditingService(const PointCloudEditingService&) = delete;
        PointCloudEditingService& operator=(const PointCloudEditingService&) = delete;

        // Main thread. Null with a diagnostic when the entity, the references or the rows do not fit.
        [[nodiscard]] std::shared_ptr<PointEdit> Capture(const PointEditRequest& request, std::string& diagnostic);
        [[nodiscard]] PointEditView View(const PointEdit& edit) const;
        // Main thread: inputs and output unchanged since the capture.
        [[nodiscard]] bool Current(const PointEdit& edit);
        // The cached CPU LBVH over the captured points (with AcquireIndex), indices compact; any thread.
        [[nodiscard]] const Geometry::PointLBVH::Index* CpuIndex(const PointEdit& edit) const;
        // Main thread: GPU neighbourhood queries can run (framed spatial cache and job service).
        [[nodiscard]] bool GpuQueriesAvailable();
        // Main thread, once per frame until Ready or Failed: appends `width` compact neighbour indices (self
        // included) per point of `points`. The first pass queries the cached index; after RestartGpuKnnRows a
        // pass queries a private index over the moved `points`.
        [[nodiscard]] PointRowsState AdvanceGpuKnnRows(PointEdit& edit, std::span<const glm::vec3> points,
                                                       std::uint32_t width, std::uint32_t batchSize,
                                                       std::vector<std::uint32_t>& rows, std::string& diagnostic);
        void RestartGpuKnnRows(PointEdit& edit);
        [[nodiscard]] PointGpuRowStats GpuRowStats(const PointEdit& edit) const;
        // Main thread: writes `live` (one position per captured point) into the output for the live slots, records
        // one undo entry named `label` and marks the entity dirty. Stale when an input or the output changed.
        [[nodiscard]] PointPublishStatus Publish(PointEdit& edit, std::span<const glm::vec3> live, std::string label);
        // The entity's vec3 point properties with at least `minimumLiveCount` live rows, for pickers.
        [[nodiscard]] GeometryPropertyCatalogSnapshot InputCatalog(std::uint32_t stableEntityId,
                                                                   std::size_t minimumLiveCount);
        // The app's editor workspace (the Sandbox shell's), so edits share its history guards and session.
        void UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept;
        // Before that workspace detaches: back to the service's own.
        void ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept;

        struct Impl;

    private:
        std::unique_ptr<Impl> m_Impl;
        friend class PointCloudEditingModule;
    };

    class PointCloudEditingModule final : public IRuntimeModule
    {
    public:
        PointCloudEditingModule();
        ~PointCloudEditingModule() override;
        // After "Runtime.SceneDocumentModule" in name order, so it shuts down (and detaches its own workspace)
        // before the document it observes.
        [[nodiscard]] std::string_view Name() const noexcept override { return "Runtime.Services.PointCloudEditing"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        [[nodiscard]] RuntimeModuleResult OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        PointCloudEditingService m_Service{};
    };
}
