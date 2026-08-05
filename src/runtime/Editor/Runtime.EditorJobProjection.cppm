module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.EditorJobProjection;

import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;

export namespace Extrinsic::Runtime
{
    enum class EditorJobScope : std::uint8_t
    {
        Unknown,
        MeshVertex,
        MeshEdge,
        MeshHalfedge,
        MeshFace,
        MeshSurface,
        GraphNode,
        GraphHalfedge,
        GraphEdge,
        PointCloudPoint,
    };
    [[nodiscard]] EditorJobScope ToEditorJobScope(
        GeometryElementDomain domain) noexcept;
    struct EditorJobIdentity
    {
        std::uint32_t EntityId{0u};
        EditorJobScope Scope{EditorJobScope::Unknown};
        GeometryPresentationSlotSemantic OutputSemantic{GeometryPresentationSlotSemantic::Albedo};
        std::string OutputName{};
    };
    [[nodiscard]] bool SameEditorJobOutput(
        const EditorJobIdentity& lhs,
        const EditorJobIdentity& rhs) noexcept;
    [[nodiscard]] bool IsActiveEditorJobState(JobState state) noexcept;
    [[nodiscard]] bool IsFailedEditorJobState(JobState state) noexcept;
    struct EditorJobDependency
    {
        JobToken Job{};
        std::string Reason{};
    };
    enum class EditorJobDomain : std::uint8_t
    {
        Cpu,
        GpuCompute,
        GpuGraphics,
        Auto,
    };
    struct EditorJobRecord
    {
        JobToken Token{};
        EditorJobIdentity Identity{};
        std::string Name{};
        JobState State{JobState::Invalid};
        EditorJobDomain RequestedJobDomain{EditorJobDomain::Cpu};
        EditorJobDomain ResolvedJobDomain{EditorJobDomain::Cpu};
        std::vector<EditorJobDependency> Dependencies{};
        float NormalizedProgress{0.0f};
        bool ProgressDeterminate{true};
        bool PreviousOutputRetained{false};
        std::uint64_t PayloadToken{0u};
        std::uint64_t ElapsedMilliseconds{0u};
        std::string Diagnostic{};
    };
    struct EditorJobQueueSnapshot
    {
        std::vector<EditorJobRecord> Entries{};
    };
    struct EditorJobDependencyModel
    {
        JobToken Job{};
        std::string Reason{};
    };
    struct EditorJobModel
    {
        JobToken Handle{};
        EditorJobIdentity Key{};
        std::string Name{};
        EditorJobDomain RequestedJobDomain{EditorJobDomain::Cpu};
        EditorJobDomain ResolvedJobDomain{EditorJobDomain::Cpu};
        JobState Status{JobState::Queued};
        std::vector<EditorJobDependencyModel> Dependencies{};
        float NormalizedProgress{0.0f};
        bool ProgressDeterminate{true};
        bool PreviousOutputRetained{false};
        std::uint64_t PayloadToken{0u};
        std::uint64_t ElapsedMilliseconds{0u};
        std::string Diagnostic{};
    };
    struct EditorJobCommandSurface
    {
        std::function<JobToken(JobDesc, EditorJobIdentity)> Submit{};
        std::function<std::optional<EditorJobRecord>(
            const EditorJobIdentity&)>
            FindActive{};
        std::function<std::vector<EditorJobRecord>(std::uint32_t)>
            SnapshotEntity{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Submit);
        }
    };
}
