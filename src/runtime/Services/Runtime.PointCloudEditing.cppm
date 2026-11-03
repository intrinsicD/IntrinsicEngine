// ARCH-022: point-cloud edits for plugins. Captures an entity's live points and normals, gives neighbourhood rows
// (k nearest or within a radius) on the CPU or the GPU, and publishes new positions into a vec3 output, or values
// into several scalar outputs, with one undo entry. It wraps the
// editor's shared point helpers, so a plugin imports no editor module, and it holds no plugin callable: undo
// entries hold host data, so they work after the plugin that recorded them is unloaded.
module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
    // What to read and where to write: a vec3 `Output` (Publish), or with `ScalarOutputs` one scalar column each
    // (PublishScalars; `Output` is unused then). Unknown domains resolve to the entity's primary point domain, the
    // normals' and the outputs' to the positions'.
    struct PointEditRequest
    {
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Positions{};
        GeometryPropertyRef Normals{};
        GeometryPropertyRef Output{};
        std::vector<GeometryPropertyRef> ScalarOutputs{};
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
        bool HasZeroNormals{false}; // a live normal of length zero
        GeometryPropertyRef Output{}; // with its resolved domain
        std::span<const GeometryPropertyRef> ScalarOutputs{}; // with their resolved domains
    };

    // Neighbour rows within a radius, compact indices: point i's neighbours are Indices[Offsets[i], Offsets[i+1]),
    // ascending, self excluded.
    struct PointNeighbourRows
    {
        std::vector<std::uint32_t> Offsets{0u}, Indices{};
        std::size_t MaximumNeighbors{0u}; // the largest support found, before a lowest-ID limit
    };

    enum class PointRowsState : std::uint8_t { Pending, Ready, Failed };

    struct PointGpuRowStats
    {
        std::size_t QueryBatches{0u}, WorkspaceBuilds{0u};
        double Milliseconds{0.0};
    };

    // Unrepresentable: a value does not fit a scalar output's storage exactly (nothing is written).
    enum class PointPublishStatus : std::uint8_t { Applied, Stale, Rejected, Unrepresentable };

    // A plugin window's picker over an InputCatalog (ImGui, the host's context): true when the choice changed `ref`;
    // a `ref` the catalog does not list stays as it is.
    bool PointPropertyCombo(const char* label, GeometryPropertyRef& ref, const GeometryPropertyCatalogSnapshot& catalog);

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
        // Rows within `radius` (with AcquireIndex). A positive `lowestIdLimit` keeps a point's lowest-ID neighbours
        // up to that many; 0 requires the complete support. CPU: from the cached index, any thread. GPU: main thread,
        // once per frame until Ready or Failed, pages of `batchSize` queries with `capacity` candidates each; Failed
        // when the inputs changed. False or Failed with a diagnostic when a support is incomplete.
        [[nodiscard]] bool CpuRadiusRows(const PointEdit& edit, float radius, std::uint32_t lowestIdLimit,
                                         PointNeighbourRows& rows, std::string& diagnostic) const;
        [[nodiscard]] PointRowsState AdvanceGpuRadiusRows(PointEdit& edit, float radius, std::uint32_t batchSize,
                                                          std::uint32_t capacity, std::uint32_t lowestIdLimit,
                                                          PointNeighbourRows& rows, std::string& diagnostic);
        [[nodiscard]] PointGpuRowStats GpuRowStats(const PointEdit& edit) const;
        // Main thread: writes `live` (one position per captured point) into the output for the live slots, records
        // one undo entry named `label` and marks the entity dirty. Stale when an input or the output changed.
        [[nodiscard]] PointPublishStatus Publish(PointEdit& edit, std::span<const glm::vec3> live, std::string label);
        // Main thread: writes one column per scalar output (one value per captured point) for the live slots, all or
        // none, as one undo entry named `label`. Deleted slots keep their values.
        [[nodiscard]] PointPublishStatus PublishScalars(PointEdit& edit, std::span<const std::vector<float>> columns,
                                                        std::string label);
        // Main thread: shows a property of the entity in the viewport (the processing panels' "Show"); false with
        // the reason when the editor refuses.
        [[nodiscard]] bool ShowProperty(std::uint32_t stableEntityId, const GeometryPropertyRef& property,
                                        std::string& diagnostic);
        // The entity's vec3 point properties with at least `minimumLiveCount` live rows, for pickers; with `domain`,
        // only that domain's (a normals picker offers the positions' domain).
        [[nodiscard]] GeometryPropertyCatalogSnapshot InputCatalog(
            std::uint32_t stableEntityId, std::size_t minimumLiveCount,
            std::optional<GeometryElementDomain> domain = std::nullopt);
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
