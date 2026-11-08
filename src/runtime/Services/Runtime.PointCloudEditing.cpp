module;

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

module Extrinsic.Runtime.PointCloudEditing;

import Geometry.Properties;
import Geometry.Validation;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.RHI.Device;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSlot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PointScalarTransaction;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"

namespace Extrinsic::Runtime
{
    namespace Detail = GeometryProcessingDetail;
    namespace ECSC = ECS::Components;
    using D = GeometryElementDomain;

    namespace
    {
        // One completed kNN page appended as compact rows: original source IDs mapped through `slots`, or (a private
        // target over the moved points, empty `slots`) identity rows below `rows`.
        bool AppendGpuKnnPage(const SpatialNearestBatch& batch,
            std::uint32_t width,
            std::span<const std::uint32_t> slots,
            std::size_t rows,
            std::vector<std::uint32_t>& out,
            std::string& why)
        {
            for (std::size_t row = 0; row < batch.Counts.size(); ++row)
            {
                if (batch.Counts[row] != width)
                {
                    why = "Incomplete GPU kNN neighbourhood.";
                    return false;
                }
                for (std::size_t j = 0; j < width; ++j)
                {
                    const auto id = batch.Neighbors[row * batch.Capacity + j].Index;
                    if (slots.empty())
                    {
                        if (id >= rows)
                        {
                            why = "Invalid private GPU neighbour row.";
                            return false;
                        }
                        out.push_back(id);
                        continue;
                    }
                    // The cached index names source rows: map them to compact indices.
                    const auto found = std::lower_bound(slots.begin(), slots.end(), id);
                    if (found == slots.end() || *found != id)
                    {
                        why = "Invalid GPU source row.";
                        return false;
                    }
                    out.push_back(std::uint32_t(found - slots.begin()));
                }
            }
            return true;
        }
    }

    struct PointGpuRows
    {
        std::shared_ptr<SpatialQuerySession> First{}, Moving{}; // the cached index; the moved points (kNN, Moving)
        std::vector<std::uint32_t> Slots{}; // the capture's source rows
        std::uint32_t Width{}, LowestIdLimit{};
        bool Moved{}; // a pass completed: later kNN passes query the moved points (worker)
        std::atomic<bool> Abort{};
        std::atomic<std::uint64_t> Microseconds{}, Builds{};
    };

    struct PointEdit
    {
        EditorProcessingContext Context{}; // a copy, as today's operations keep in their callbacks
        entt::entity Entity{};
        PointEditRequest Request{};
        Detail::PointNormalCapture Capture{};
        Detail::PointPropertyWatch OutputWatch{};
        std::vector<glm::vec3> BeforeOutput{}, AfterOutput{};
        std::vector<Detail::PointPropertyWatch> ScalarWatches{};
        std::vector<GeometryScalarPropertySnapshot> ScalarBefore{};
        std::shared_ptr<const SpatialIndexSnapshot> Index{};
        SpatialIndexHandle GpuIndex{};
        bool IndexReused{false};
        std::uint64_t PreviewRing{0u}; // the generation of the positions' ring PreviewPositions made; 0: none
        std::optional<ECSC::Transform::Component> Transform{}; // with CaptureTransform (empty: none)
        glm::dmat4 Model{1.0};
    };

    struct PointCloudEditingService::Impl
    {
        EditorWorkspaceSlot Workspace{};
        // The scalar runs started here (owned by their handles), ended when their workspace stops being used.
        std::vector<std::weak_ptr<ScalarAnalysis::Run>> ScalarRuns{};

        // With CaptureTransform: the entity is still unparented and its local Transform as captured (or still none).
        static bool TransformCurrent(const PointEdit& edit)
        {
            if (!edit.Request.CaptureTransform) return true;
            const auto& raw = edit.Context.Scene->Raw();
            if (!raw.valid(edit.Entity)) return false;
            if (const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(edit.Entity);
                hierarchy != nullptr && raw.valid(hierarchy->Parent))
                return false;
            const auto* transform = raw.try_get<ECSC::Transform::Component>(edit.Entity);
            return edit.Transform ? transform != nullptr && EditorFeatureDetail::SameTransformComponent(*transform, *edit.Transform)
                                  : transform == nullptr;
        }

        static bool Finite(const std::span<const glm::vec3> values)
        {
            return std::ranges::all_of(values, [](const glm::vec3& v) { return Geometry::Validation::IsFinite(v); });
        }

        // The capture's world is still the active one and its inputs, outputs and (if captured) Transform unchanged.
        static bool InputsCurrent(const PointEdit& edit)
        {
            const std::array outputs{edit.OutputWatch};
            return Detail::EditorProcessingContextWorldCurrent(edit.Context) && TransformCurrent(edit) &&
                Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, edit.Capture.Inputs) &&
                (!edit.Request.ScalarOutputs.empty() ||
                 Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, outputs)) &&
                Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, edit.ScalarWatches);
        }
    };

    PointCloudEditingService::PointCloudEditingService() : m_Impl(std::make_unique<Impl>()) {}
    PointCloudEditingService::~PointCloudEditingService() = default;

    void EndScalarRunsOutside(std::vector<std::weak_ptr<ScalarAnalysis::Run>>& runs, const void* workspace) noexcept;

    void PointCloudEditingService::UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace.Use(workspace);
        EndScalarRunsOutside(m_Impl->ScalarRuns, m_Impl->Workspace.Current());
    }

    void PointCloudEditingService::ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace.Release(workspace);
        EndScalarRunsOutside(m_Impl->ScalarRuns, m_Impl->Workspace.Current());
    }

    std::shared_ptr<PointEdit> PointCloudEditingService::Capture(const PointEditRequest& request,
                                                                  std::string& diagnostic)
    {
        const auto fail = [&](std::string why) -> std::shared_ptr<PointEdit>
        {
            diagnostic = std::move(why);
            return {};
        };
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        const EditorProcessingContext& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.Scene == nullptr) return fail("Scene is unavailable.");
        const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), request.StableEntityId);
        if (!entity) return fail((request.Noun.empty() ? "The" : request.Noun) + " target entity is stale or missing.");
        auto edit = std::make_shared<PointEdit>();
        edit->Context = context;
        edit->Entity = *entity;
        edit->Request = request;
        auto& r = edit->Request;
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        if (r.Positions.Domain == D::Unknown) r.Positions.Domain = Detail::PrimaryPointDomain(a);
        if (r.Output.Domain == D::Unknown) r.Output.Domain = r.Positions.Domain;
        if (r.Normals.Domain == D::Unknown) r.Normals.Domain = r.Positions.Domain;
        for (auto& output : r.ScalarOutputs)
            if (output.Domain == D::Unknown) output.Domain = r.Positions.Domain;
        const auto* props = ResolveGeometryPropertySet(a, r.Positions.Domain);
        if (props == nullptr || !ResolveGeometryProperty(a, r.Positions, props->Size(), false).Resolved())
            return fail("Choose a count-matched vec3 position property on a resolved element domain.");
        const bool scalar = !r.ScalarOutputs.empty();
        for (std::size_t i = 0; i < r.ScalarOutputs.size(); ++i)
        {
            const auto& output = r.ScalarOutputs[i];
            if (output.Domain != r.Positions.Domain || output.Name == r.Positions.Name ||
                output.Name == r.Normals.Name ||
                std::any_of(r.ScalarOutputs.begin(), r.ScalarOutputs.begin() + std::ptrdiff_t(i),
                            [&](const auto& earlier) { return earlier.Name == output.Name; }))
                return fail("The outputs must be distinct properties on the input domain.");
            if (IsTopologyProperty(output.Domain, output.Name))
                return fail("The outputs cannot replace topology or deletion properties.");
            if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                return fail("The outputs must be absent or count-matched scalar properties.");
        }
        if (!scalar && (r.Output.Domain != r.Positions.Domain ||
                        (r.Output.Name == r.Normals.Name && r.Normals.Name != r.Positions.Name)))
            return fail("The output must share the input domain and cannot overwrite a distinct normal input.");
        if (!scalar && IsTopologyProperty(r.Output.Domain, r.Output.Name))
            return fail("The output cannot replace topology or deletion properties.");
        if (!scalar && props->Exists(r.Output.Name) && !ResolveGeometryProperty(a, r.Output, props->Size(), false).Resolved())
            return fail("The output must be absent or a count-matched vec3 property.");
        if (r.CaptureTransform)
        {
            if (const auto* transform = context.Scene->Raw().try_get<ECSC::Transform::Component>(*entity))
            {
                edit->Transform = *transform;
                edit->Model = ECSC::Transform::GetMatrixDouble(*transform);
            }
            if (!Impl::TransformCurrent(*edit)) // a parent
                return fail("Choose an unparented entity: its points are mapped with its local Transform.");
        }
        const bool captured = r.Normals.Name.empty()
            ? (r.CopyValues ? Detail::CapturePointInput(a, r.Positions, true, edit->Capture, diagnostic)
                            : Detail::PreparePointInput(context, *entity, a, r.Positions, edit->Capture, diagnostic))
            : Detail::CapturePointNormalInput(context, *entity, a, r.Positions, r.Normals, r.CopyValues, edit->Capture,
                                              diagnostic);
        if (!captured) return {};
        if (!scalar) edit->OutputWatch = Detail::ObserveGeometryProperty(a, r.Output.Domain, r.Output.Name);
        for (const auto& output : r.ScalarOutputs)
            edit->ScalarWatches.push_back(Detail::ObserveGeometryProperty(a, output.Domain, output.Name));
        if (edit->Capture.LiveCount < r.MinimumLiveCount)
            return fail("At least " + std::to_string(r.MinimumLiveCount) + " live samples are required.");
        if (r.CopyValues && scalar)
            for (const auto& output : r.ScalarOutputs)
                edit->ScalarBefore.push_back(CaptureGeometryScalarProperty(*props, output));
        if (r.CopyValues && !scalar)
        {
            if (edit->OutputWatch.Revision) edit->BeforeOutput = props->Get<glm::vec3>(r.Output.Name).Vector();
            edit->AfterOutput = edit->OutputWatch.Revision ? edit->BeforeOutput
                                                           : props->Get<glm::vec3>(r.Positions.Name).Vector();
        }
        if (r.AcquireIndex && !AcquireIndex(*edit, diagnostic)) return {};
        return edit;
    }

    bool PointCloudEditingService::AcquireIndex(PointEdit& edit, std::string& diagnostic)
    {
        const auto& context = edit.Context;
        if (context.SpatialIndices == nullptr)
        {
            diagnostic = "The spatial index cache is unavailable.";
            return false;
        }
        const auto state = Detail::AcquirePointIndex(*context.SpatialIndices,
            context.World,
            edit.Entity,
            edit.Request.Positions,
            edit.Capture.Slots,
            edit.Capture.Points,
            edit.GpuIndex,
            edit.Index,
            edit.IndexReused,
            diagnostic,
            edit.Request.CpuIndex);
        if (state == Detail::PointIndexState::Mismatched)
            diagnostic = "The index snapshot does not match the selected samples.";
        return state == Detail::PointIndexState::Ready;
    }

    PointEditView PointCloudEditingService::View(const PointEdit& edit) const
    {
        return {.Points = edit.Capture.Points,
                .PointNormals = edit.Capture.Normals,
                .SlotCount = edit.Capture.SlotCount,
                .LiveCount = edit.Capture.LiveCount,
                .ValidLbvh = edit.Capture.ValidLbvh,
                .IndexReused = edit.IndexReused,
                .HasZeroNormals = edit.Capture.HasZeroNormals,
                .Output = edit.Request.Output,
                .ScalarOutputs = edit.Request.ScalarOutputs,
                .Model = edit.Model};
    }

    bool PointCloudEditingService::Current(const PointEdit& edit) { return Impl::InputsCurrent(edit); }

    const Geometry::PointLBVH::Index* PointCloudEditingService::CpuIndex(const PointEdit& edit) const
    {
        return edit.Index && edit.Index->Cpu ? &edit.Index->Index : nullptr;
    }

    bool PointCloudEditingService::GpuQueriesAvailable()
    {
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        const EditorProcessingContext& context = EditorProcessingCommandsAccess::Resolve(commands);
        return context.JobCommands.Available() && context.SpatialIndices != nullptr &&
            context.SpatialIndices->GpuQueriesAvailable();
    }

    std::shared_ptr<PointGpuRows> PointCloudEditingService::PrepareGpuRows(
        PointEdit& edit, const PointGpuRowsRequest& request, std::string& diagnostic)
    {
        auto* cache = edit.Context.SpatialIndices;
        if (cache == nullptr || !edit.GpuIndex.Value)
        {
            diagnostic = "GPU rows need the spatial index cache and a capture made with AcquireIndex.";
            return {};
        }
        if (!Impl::InputsCurrent(edit))
        {
            diagnostic = "The inputs changed.";
            return {};
        }
        const auto& points = edit.Capture.Points;
        const bool radius = request.Width == 0u;
        if (points.size() < 2u || (radius && request.Capacity == 0u) || request.BatchSize == 0u)
        {
            diagnostic = "Invalid GPU row request.";
            return {};
        }
        if (radius && request.Strategy != Graphics::PointQueryStrategy::Lbvh)
        {
            diagnostic = "GPU radius rows use the LBVH strategy; brute force serves kNN rows only.";
            return {};
        }
        auto rows = std::make_shared<PointGpuRows>();
        rows->Slots = edit.Capture.Slots;
        rows->Width = request.Width;
        rows->LowestIdLimit = request.LowestIdLimit;
        // Radius rows exclude each query's own source row; PointRadiusCapacity bounds their candidates.
        const SpatialQuerySessionDesc first{.Strategy = request.Strategy,
            .Radius = radius,
            .Capacity = radius ? Detail::PointRadiusCapacity(request.Capacity, points.size(), request.LowestIdLimit)
                               : request.Width,
            .PageQueries = request.BatchSize,
            .Queries = points,
            .Excluded = radius ? std::span<const std::uint32_t>(edit.Capture.Slots) : std::span<const std::uint32_t>{}};
        rows->First = cache->PrepareQuerySession(edit.GpuIndex, first, diagnostic);
        if (!rows->First)
            return {};
        if (!radius && request.Moving)
        {
            auto moving = first;
            moving.Moving = true;
            rows->Moving = cache->PrepareQuerySession({}, moving, diagnostic);
            if (!rows->Moving)
                return {};
        }
        return rows;
    }

    PointRowsState ComputeGpuKnnRows(PointGpuRows& rows,
        const std::span<const glm::vec3> points,
        std::vector<std::uint32_t>& out,
        std::string& diagnostic)
    {
        const auto started = std::chrono::steady_clock::now();
        const bool moved = rows.Moved && rows.Moving;
        auto& session = moved ? *rows.Moving : *rows.First;
        if (rows.Width == 0u || points.size() != session.Rows())
        {
            diagnostic = "GPU kNN rows need a kNN request and one point per captured point.";
            return PointRowsState::Failed;
        }
        if (moved)
        {
            if (!session.UploadQueries(points, diagnostic))
                return PointRowsState::Failed;
            if (rows.Builds.load(std::memory_order_relaxed) == 0u)
                rows.Builds.store(1u, std::memory_order_relaxed); // the private target, prepared once
        }
        out.clear();
        out.reserve(points.size() * rows.Width);
        const auto slots = moved ? std::span<const std::uint32_t>{} : std::span<const std::uint32_t>(rows.Slots);
        SpatialNearestBatch batch;
        for (std::uint32_t first = 0; first < session.Rows();)
        {
            const auto count = std::min(session.PageQueries(), session.Rows() - first);
            switch (session.Query(first, count, -1.f, nullptr, rows.Abort, batch))
            {
            case SpatialPageStatus::Abandoned: return PointRowsState::Pending;
            case SpatialPageStatus::Failed:    diagnostic = batch.Diagnostic; return PointRowsState::Failed;
            case SpatialPageStatus::Ready:     break;
            }
            if (!AppendGpuKnnPage(batch, rows.Width, slots, points.size(), out, diagnostic))
                return PointRowsState::Failed;
            first += count;
        }
        rows.Moved = true;
        rows.Microseconds.fetch_add(std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - started)
                                            .count()),
            std::memory_order_relaxed);
        return PointRowsState::Ready;
    }

    PointRowsState ComputeGpuRadiusRows(
        PointGpuRows& rows, const float radius, PointNeighbourRows& out, std::string& diagnostic)
    {
        if (rows.Width != 0u)
        {
            diagnostic = "GPU radius rows need a radius request.";
            return PointRowsState::Failed;
        }
        Detail::PointRadiusRows packed{};
        const auto state = Detail::ComputePointRadiusRows(
            *rows.First, rows.Slots, radius, rows.LowestIdLimit, rows.Abort, packed, diagnostic);
        out.MaximumNeighbors = packed.MaximumNeighbors;
        rows.Microseconds.fetch_add(std::uint64_t(packed.Milliseconds * 1000.0), std::memory_order_relaxed);
        if (state != Detail::RowsState::Ready)
            return state == Detail::RowsState::Pending ? PointRowsState::Pending : PointRowsState::Failed;
        out.Offsets = std::move(packed.Offsets);
        out.Indices = std::move(packed.Indices);
        return PointRowsState::Ready;
    }

    void AbortGpuRows(PointGpuRows& rows) noexcept
    {
        rows.Abort.store(true, std::memory_order_release);
    }

    PointGpuRowStats GpuRowStats(const PointGpuRows& rows) noexcept
    {
        PointGpuRowStats stats{.WorkspaceBuilds = std::size_t(rows.Builds.load(std::memory_order_relaxed)),
            .Milliseconds = double(rows.Microseconds.load(std::memory_order_relaxed)) / 1000.0};
        for (const auto* session : {rows.First.get(), rows.Moving.get()})
            if (session)
            {
                stats.QueryBatches += std::size_t(session->Pages());
                stats.ReadbackBytes += session->ReadbackBytes();
                (session == rows.Moving.get() ? stats.WorkspaceUploadBytes : stats.QueryUploadBytes) +=
                    session->UploadBytes();
            }
        return stats;
    }

    bool PointCloudEditingService::CpuRadiusRows(const PointEdit& edit, const float radius,
                                                 const std::uint32_t lowestIdLimit, PointNeighbourRows& rows,
                                                 std::string& diagnostic) const
    {
        if (!edit.Index || !edit.Index->Cpu)
        {
            diagnostic = "The CPU index is unavailable.";
            return false;
        }
        const auto& points = edit.Capture.Points;
        Detail::PointRadiusRows packed{};
        std::vector<std::uint32_t> row;
        for (std::uint32_t i = 0; i < points.size(); ++i)
        {
            const auto others = std::uint32_t(points.size() - 1);
            const auto capacity = lowestIdLimit ? std::min(lowestIdLimit, others) : others;
            const auto neighbors = edit.Index->Index.Radius(points[i], radius, capacity, i);
            packed.MaximumNeighbors = std::max(packed.MaximumNeighbors, std::size_t(neighbors.TotalCount));
            const auto required = lowestIdLimit ? std::min(lowestIdLimit, neighbors.TotalCount) : neighbors.TotalCount;
            if (neighbors.Neighbors.size() < required)
            {
                diagnostic = "Incomplete CPU radius support.";
                return false;
            }
            row.clear();
            for (const auto& neighbor : neighbors.Neighbors) row.push_back(neighbor.Index);
            if (!Detail::AppendPointRadiusRow(packed, row, diagnostic)) return false;
        }
        rows = {.Offsets = std::move(packed.Offsets), .Indices = std::move(packed.Indices),
                .MaximumNeighbors = packed.MaximumNeighbors};
        return true;
    }

    PointPreviewStatus PointCloudEditingService::PreviewPositions(PointEdit& edit, const std::span<const glm::vec3> live,
                                                                  std::string& diagnostic)
    {
        const auto fail = [&](const PointPreviewStatus status, std::string why)
        {
            diagnostic = std::move(why);
            return status;
        };
        constexpr auto kUnavailable = PointPreviewStatus::Unavailable;
        const auto& r = edit.Request;
        if (live.size() != edit.Capture.Slots.size()) return fail(kUnavailable, "One position per captured point is needed.");
        if (r.Output.Domain != r.Positions.Domain || r.Output.Name != r.Positions.Name)
            return fail(kUnavailable, "Only an in-place output (the positions) shows a preview.");
        if (!Impl::InputsCurrent(edit)) return fail(kUnavailable, "The inputs changed.");
        if (!Impl::Finite(live)) return fail(kUnavailable, "A position is not finite.");
        const auto& context = edit.Context;
        auto* residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
        if (residency == nullptr || context.Device == nullptr || !context.Device->IsOperational())
            return fail(kUnavailable, "No operational GPU device shows previews.");
        const auto key = MakeGpuPropertyKey(context.World, edit.Entity, r.Output);
        if (residency->HasRing(key) && residency->RingGeneration(key) != edit.PreviewRing)
            return fail(PointPreviewStatus::Busy, "Another result holds these positions' GPU ring.");
        // The publication's scratch: deleted slots keep their positions, and Publish writes the same live slots.
        auto& values = edit.AfterOutput;
        for (std::size_t i = 0; i < live.size(); ++i) values[edit.Capture.Slots[i]] = live[i];
        const auto layout = MakeGpuPropertyLayout(Geometry::PropertyValueKind::Vec3, static_cast<std::uint32_t>(values.size()));
        if (!layout) return fail(kUnavailable, "The positions have no GPU layout.");
        if (!UploadGpuPropertyFront(*residency, *context.Device, key, *layout, 2u, std::as_bytes(std::span(values))))
            return fail(PointPreviewStatus::Busy, "No free preview slot this frame.");
        edit.PreviewRing = residency->RingGeneration(key);
        return PointPreviewStatus::Shown;
    }

    void PointCloudEditingService::DiscardPreview(PointEdit& edit)
    {
        const auto ring = std::exchange(edit.PreviewRing, 0u);
        auto* residency = edit.Context.SpatialIndices ? edit.Context.SpatialIndices->PropertyResidency() : nullptr;
        if (ring != 0u && residency != nullptr)
            (void)residency->Discard(MakeGpuPropertyKey(edit.Context.World, edit.Entity, edit.Request.Output), ring);
    }

    PointPublishStatus PointCloudEditingService::Publish(PointEdit& edit, const std::span<const glm::vec3> live,
                                                         std::string label)
    {
        DiscardPreview(edit); // the canonical publication shows next
        // Staleness: checked here (the capture's revisions), then by the history entry on every redo and undo.
        const auto& slots = edit.Capture.Slots;
        if (live.size() != slots.size() || !Impl::Finite(live)) return PointPublishStatus::Rejected;
        for (std::size_t i = 0; i < slots.size(); ++i) edit.AfterOutput[slots[i]] = live[i];
        struct State
        {
            bool Exists{};
            std::vector<glm::vec3> Values{};
        };
        auto before = std::make_shared<State>(State{bool(edit.OutputWatch.Revision), edit.BeforeOutput});
        auto after = std::make_shared<State>(State{true, edit.AfterOutput});
        auto inputs = edit.Capture.Inputs;
        const auto& r = edit.Request;
        std::erase_if(inputs, [&](const auto& input) { return input.Domain == r.Output.Domain && input.Name == r.Output.Name; });
        // The output's guard is the history slot every author of that property shares, so successive edits (and
        // other commands writing it, such as a GPU position accept) undo in turn; each write stamps it.
        if (!Detail::EditorProcessingContextWorldCurrent(edit.Context) || !Impl::TransformCurrent(edit) ||
            !Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, inputs) ||
            !Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, std::span(&edit.OutputWatch, 1)))
            return PointPublishStatus::Stale;
        auto outputWatches = Detail::CapturePointPropertyHistory(edit.Context, edit.Entity, std::span(&r.Output, 1));
        // Host code only: the entry works whatever happened to the plugin that recorded it.
        const auto mutate = [context = edit.Context, entity = edit.Entity, inputs = std::move(inputs), r,
                             outputWatches = std::move(outputWatches)](const State& target)
        {
            if (!Detail::GeometryPropertiesCurrent(context, entity, inputs) ||
                !Detail::PointPropertyHistoryCurrent(context, entity, outputWatches))
                return EditorCommandHistoryStatus::StaleEntity;
            auto* props = Detail::MutableGeometryProperties(context.Scene->Raw(), entity, r.Positions.Domain);
            if (target.Exists)
                props->GetOrAdd<glm::vec3>(r.Output.Name).Vector() = target.Values;
            else if (auto p = props->Get<glm::vec3>(r.Output.Name))
                props->Remove(p);
            Detail::StampPointPropertyHistory(context, entity, outputWatches);
            ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(), entity);
            ECS::Components::DirtyTags::MarkVertexPositionsDirty(context.Scene->Raw(), entity);
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        const auto status = edit.Context.CommandHistory != nullptr
            ? edit.Context.CommandHistory
                  ->Execute({.Label = std::move(label),
                             .Redo = [mutate, after] { return mutate(*after); },
                             .Undo = [mutate, before] { return mutate(*before); }})
                  .Status
            : mutate(*after);
        if (status == EditorCommandHistoryStatus::Applied) return PointPublishStatus::Applied;
        return status == EditorCommandHistoryStatus::StaleEntity ? PointPublishStatus::Stale
                                                                 : PointPublishStatus::Rejected;
    }

    PointPublishStatus PointCloudEditingService::PublishScalars(PointEdit& edit,
                                                                const std::span<const std::vector<float>> columns,
                                                                std::string label)
    {
        const auto& r = edit.Request;
        const auto& slots = edit.Capture.Slots;
        if (columns.size() != r.ScalarOutputs.size() || edit.ScalarBefore.size() != columns.size())
            return PointPublishStatus::Rejected;
        // The world first: a Transform check reads the captured scene.
        if (!Detail::EditorProcessingContextWorldCurrent(edit.Context) || !Impl::TransformCurrent(edit))
            return PointPublishStatus::Stale;
        // Indexed by source slot, as the publisher reads them; it writes the live slots only.
        std::vector<std::vector<float>> after(columns.size(), std::vector<float>(edit.Capture.SlotCount));
        std::vector<Detail::PointScalarOutput> outputs(columns.size());
        for (std::size_t c = 0; c < columns.size(); ++c)
        {
            if (columns[c].size() != slots.size()) return PointPublishStatus::Rejected;
            for (std::size_t i = 0; i < slots.size(); ++i) after[c][slots[i]] = columns[c][i];
            outputs[c] = {.Output = r.ScalarOutputs[c], .Watch = edit.ScalarWatches[c], .Before = &edit.ScalarBefore[c],
                          .After = after[c]};
        }
        const auto status = Detail::PublishPointScalarField(edit.Context, edit.Entity, edit.Capture, outputs,
                                                            std::move(label));
        if (status == EditorCommandHistoryStatus::Applied) return PointPublishStatus::Applied;
        if (status == EditorCommandHistoryStatus::InvalidCommand) return PointPublishStatus::Unrepresentable;
        return status == EditorCommandHistoryStatus::StaleEntity ? PointPublishStatus::Stale
                                                                 : PointPublishStatus::Rejected;
    }

    bool PointCloudEditingService::ShowProperty(const std::uint32_t stableEntityId, const GeometryPropertyRef& property,
                                                std::string& diagnostic)
    {
        auto* workspace = m_Impl->Workspace.Current();
        if (workspace == nullptr)
        {
            diagnostic = "No editor workspace.";
            return false;
        }
        const auto status = ApplyEditorVisualizationRecipeCommand(
            PrepareEditorVisualizationEditingFrame(*workspace).Commands,
            {.StableEntityId = stableEntityId, .Recipe = MakeEditorPropertyVisualizationRecipe(property)});
        diagnostic = DebugNameForEditorCommandStatus(status);
        return status == EditorCommandStatus::Applied || status == EditorCommandStatus::NoChange;
    }

    GeometryPropertyCatalogSnapshot PointCloudEditingService::InputCatalog(
        const std::uint32_t stableEntityId, const std::size_t minimumLiveCount,
        const std::optional<GeometryElementDomain> domain)
    {
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        auto catalog = Detail::BuildPointInputCatalog(EditorProcessingCommandsAccess::Resolve(commands), stableEntityId,
                                                      minimumLiveCount);
        if (domain) std::erase_if(catalog.Entries, [&](const auto& entry) { return entry.Ref.Domain != *domain; });
        return catalog;
    }

    struct ScalarAnalysis::Run
    {
        EditorPointScalarTransactionHandle Transaction{};
        std::function<bool()> AttachmentActive{}; // the attachment the run was started in
        const void* Workspace{nullptr};           // the workspace the service used then
        std::size_t SlotCount{0u};                // of the captured domain
        GeometryPropertyRef Output{};             // with its resolved domain
        StepRunsService* StepRuns{nullptr}; // the context's, whose run controls the transaction
        bool Released{false};                     // the service stopped using that workspace
    };

    // Discards every live run started in another workspace than `workspace` (the one the service uses now).
    void EndScalarRunsOutside(std::vector<std::weak_ptr<ScalarAnalysis::Run>>& runs, const void* workspace) noexcept
    {
        std::erase_if(runs, [workspace](const std::weak_ptr<ScalarAnalysis::Run>& weak)
        {
            const auto run = weak.lock();
            if (!run) return true;
            if (run->Workspace == workspace) return false;
            run->Released = true;
            DiscardEditorPointScalar({}, run->Transaction);
            return true;
        });
    }

    namespace
    {
        // The checks of a point scalar method's capture stage, in the operation runtime's order and with its texts
        // (`OperationRuntime::Capture`); `copyValues` as an execution captures. Empty: the capture can run.
        std::string CaptureScalarAnalysis(const EditorProcessingContext& context, const ScalarAnalysisRequest& request,
                                          const ScalarAnalysisBackend backend, const bool copyValues,
                                          Detail::PointScalarCapture& capture, entt::entity& entity,
                                          GeometryPropertyRef& positions)
        {
            if (context.Scene == nullptr) return "Scene is unavailable.";
            const auto resolved = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), request.StableEntityId);
            if (!resolved) return request.Noun + " target entity is stale or missing.";
            entity = *resolved;
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
            positions = request.Positions;
            auto output = request.Output;
            std::string why;
            if (!Detail::CapturePointScalarField(context, entity, a, positions, output, request.Noun, copyValues,
                                                 capture, why))
                return why;
            if (capture.LiveCount < request.MinimumLiveCount)
                return request.TooFewSamples.empty() ? request.Method + " requires at least two live samples."
                                                     : request.TooFewSamples;
            if (backend != ScalarAnalysisBackend::CpuReference &&
                (context.SpatialIndices == nullptr || !capture.ValidLbvh || capture.LiveCount > (1u << 24)))
                return "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.";
            if (backend == ScalarAnalysisBackend::Gpu && !Detail::AdmitPointScalarGpu(context, capture, request.Params, why))
                return why;
            return {};
        }

        ScalarAnalysisPhase PhaseOf(const EditorGpuTransactionPhase phase)
        {
            switch (phase)
            {
            case EditorGpuTransactionPhase::Running: return ScalarAnalysisPhase::Running;
            case EditorGpuTransactionPhase::ReadyToAccept: return ScalarAnalysisPhase::ReadyToAccept;
            case EditorGpuTransactionPhase::Accepting: return ScalarAnalysisPhase::Accepting;
            case EditorGpuTransactionPhase::Applied: return ScalarAnalysisPhase::Applied;
            case EditorGpuTransactionPhase::Discarded: return ScalarAnalysisPhase::Discarded;
            case EditorGpuTransactionPhase::Failed: break;
            }
            return ScalarAnalysisPhase::Failed;
        }

        ScalarAnalysis StartScalarRun(const EditorProcessingContext& context,
            const ScalarAnalysisRequest& request,
            const void* workspace,
            std::vector<std::weak_ptr<ScalarAnalysis::Run>>& runs,
            const StepRunExecution& execution,
            Graphics::GpuPropertyResidency* testResidency,
            std::vector<float> testValues,
            ScalarAnalysisRefusal& refusal)
        {
            refusal = {};
            auto capture = std::make_shared<Detail::PointScalarCapture>();
            entt::entity entity{};
            GeometryPropertyRef positions{};
            // A test run is captured as a CPU run is (no device to admit it).
            refusal.Message = CaptureScalarAnalysis(
                context, request, testResidency ? ScalarAnalysisBackend::CpuReference : ScalarAnalysisBackend::Gpu,
                true, *capture, entity, positions);
            if (!refusal.Message.empty()) return {};
            if (testResidency)
            {
                if (testValues.size() != capture->SlotCount)
                {
                    refusal.Message = "Test values must give one value per slot.";
                    return {};
                }
                capture->AfterValues = std::move(testValues);
            }
            EditorPointScalarTransactionSnapshot state;
            auto transaction = Detail::StartPointScalarGpu(context,
                capture,
                entity,
                request.StableEntityId,
                positions,
                request.Params,
                request.History,
                request.Noun + " estimation",
                state,
                {},
                execution,
                testResidency);
            if (!transaction)
            {
                refusal = {.Message = std::move(state.Message), .Busy = state.OutputBusy,
                           .Failed = state.Status == EditorCommandStatus::GeometryProcessingFailed};
                return {};
            }
            auto run = std::make_shared<ScalarAnalysis::Run>(std::move(transaction),
                context.AttachmentActive,
                workspace,
                capture->SlotCount,
                capture->Output,
                context.StepRuns);
            std::erase_if(runs, [](const auto& weak) { return weak.expired(); });
            runs.push_back(run);
            return ScalarAnalysis{std::move(run)};
        }
    }

    ScalarAnalysis::ScalarAnalysis() noexcept = default;
    ScalarAnalysis::ScalarAnalysis(std::shared_ptr<Run> run) noexcept : m_Run(std::move(run)) {}
    ScalarAnalysis::ScalarAnalysis(ScalarAnalysis&&) noexcept = default;
    ScalarAnalysis& ScalarAnalysis::operator=(ScalarAnalysis&& other) noexcept
    {
        if (this != &other)
        {
            Discard(); // the run this handle held goes with it
            m_Run = std::move(other.m_Run);
        }
        return *this;
    }
    ScalarAnalysis::~ScalarAnalysis() { Discard(); }

    void ScalarAnalysis::Discard()
    {
        // A no-op once the run ended or while its Accept publishes.
        if (m_Run) DiscardEditorPointScalar({}, m_Run->Transaction);
    }

    ScalarAnalysisState ScalarAnalysis::Poll()
    {
        if (!m_Run) return {.Message = "No scalar analysis."};
        const bool detached = m_Run->Released || (m_Run->AttachmentActive && !m_Run->AttachmentActive());
        if (detached) Discard();
        const auto s = SnapshotEditorPointScalar({}, m_Run->Transaction);
        return {.Phase = PhaseOf(s.Phase),
            .Message = s.Message,
            .AcceptRefusal = s.AcceptRefusalReason,
            .CanAccept = s.CanAccept && !detached,
            .Stale = s.Status == EditorCommandStatus::StaleEntity,
            .Detached = detached,
            .IndexReused = s.IndexReused,
            .SlotCount = m_Run->SlotCount,
            .LiveCount = s.LiveCount,
            .Output = m_Run->Output,
            .GpuInputUploadBytes = s.GpuInputUploadBytes,
            .GpuInputCacheHits = s.GpuInputCacheHits,
            .CpuStageReadbackBytes = s.CpuStageReadbackBytes,
            .GpuQueryBatches = s.GpuQueryBatches,
            .Statistics = s.Statistics,
            .StepRun = s.StepRun};
    }

    StepRunsService* ScalarAnalysis::StepRuns() const noexcept
    {
        return m_Run ? m_Run->StepRuns : nullptr;
    }

    void DrawScalarAnalysisControls(ScalarAnalysis& analysis,
        const std::uint64_t stepRun,
        const std::string_view idSuffix,
        StepRunControlsState& row)
    {
        if (!analysis)
        {
            if (auto* runs = PluginService<StepRunsService>(); runs != nullptr && stepRun != 0u)
                (void)DrawStepRunControls(*runs, stepRun, idSuffix, row);
            return;
        }
        const auto state = analysis.Poll(); // a detached run is discarded here, before any control
        if (auto* runs = analysis.StepRuns(); runs != nullptr && state.StepRun != 0u)
            (void)DrawStepRunControls(*runs, state.StepRun, idSuffix, row);
        if (state.Phase == ScalarAnalysisPhase::ReadyToAccept && !state.CanAccept && !state.AcceptRefusal.empty())
            ImGui::TextWrapped("%s", state.AcceptRefusal.c_str());
    }

    std::uint64_t PointCloudEditingService::StartScalarCpuRun(ScalarCpuRun spec, StepRunJobs jobs, std::string& refusal)
    {
        auto* runs = spec.Edit ? spec.Edit->Context.StepRuns : nullptr;
        if (runs == nullptr || !spec.Edit || !spec.Compute || !spec.Finish)
        {
            refusal = runs == nullptr ? "The step-run service is unavailable." : "A scalar CPU run is incomplete.";
            return 0u;
        }
        // Values: the column of the generation's one step (a worker writes it, Publish reads it once no step runs).
        struct State
        {
            ScalarCpuRun Spec{};
            std::vector<float> Values{};
        };
        const auto s = std::make_shared<State>(State{.Spec = std::move(spec)});
        StepRunOptions options{.Label = s->Spec.Label,
            .Owner = "point_cloud_editing",
            .Output = s->Spec.Output,
            .Execution = s->Spec.Execution,
            .MetricNames = {"live_count", "", "", ""}};
        OneShotWorkerCallbacks callbacks{.Compute =
                                             [s]
            {
                s->Values.clear();
                return s->Spec.Compute(s->Values);
            },
            // The capture's live samples, read here on the main thread (one value per live point is computed).
            .Metric =
                [live = double(s->Spec.Edit->Capture.LiveCount)](StepRunMetric& metric)
            {
                metric.Values[0] = live;
            },
            // The session first: a capture of a workspace no longer used is not read.
            .Valid =
                [this, s]
            {
                return SessionCurrent(s->Spec.Session) && Current(*s->Spec.Edit);
            },
            .Publish =
                [this, s](std::string& diagnostic)
            {
                const std::vector<std::vector<float>> columns{std::move(s->Values)};
                const auto& noun = s->Spec.Noun;
                switch (PublishScalars(*s->Spec.Edit, columns, s->Spec.History))
                {
                case PointPublishStatus::Applied: s->Spec.Finish("applied", {}, true); return true;
                case PointPublishStatus::Stale:
                    diagnostic = noun + " input or output changed before publication.";
                    break;
                case PointPublishStatus::Unrepresentable:
                    diagnostic = "Output values are not exactly representable in the selected scalar storage.";
                    s->Spec.Finish("invalid", diagnostic, true);
                    return false;
                case PointPublishStatus::Rejected:
                    diagnostic = noun + " publication rejected by history checks.";
                    s->Spec.Finish("failed", diagnostic, true);
                    return false;
                }
                s->Spec.Finish("stale", diagnostic, true);
                return false;
            },
            .Ended =
                [this, s](const StepRunObservation& run)
            {
                const auto why = UncommittedStatus(run);
                if (why == "failed") // a failed step words its own reason; a refused next step is the host's
                    s->Spec.Finish("failed", run.StepFailed ? std::string{} : run.Diagnostic, run.StepFailed);
                else if (!SessionCurrent(s->Spec.Session))
                    s->Spec.Finish("cancelled",
                        "The editor session of the " + s->Spec.Method + " run ended; nothing was written.",
                        false);
                else
                    s->Spec.Finish(Current(*s->Spec.Edit) ? why : "stale",
                        s->Spec.Label + " was cancelled or its source became stale; nothing was applied.",
                        false);
            },
            .Restart =
                [s]
            {
                s->Values = std::vector<float>{};
            }};
        return StartOneShotWorkerRun(*runs, std::move(options), std::move(callbacks), std::move(jobs), refusal);
    }

    ScalarAnalysisState ScalarAnalysis::Accept()
    {
        if (!m_Run) return {.Message = "No scalar analysis."};
        if (m_Run->Released || (m_Run->AttachmentActive && !m_Run->AttachmentActive())) return Poll();
        const auto accepted = AcceptEditorPointScalar({}, m_Run->Transaction);
        auto state = Poll();
        if (state.Phase == ScalarAnalysisPhase::ReadyToAccept) state.Message = accepted.Message; // refused: why
        return state;
    }

    std::string PointCloudEditingService::ScalarAnalysisReadiness(const ScalarAnalysisRequest& request,
                                                                  const ScalarAnalysisBackend backend, const bool current)
    {
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        Detail::PointScalarCapture capture;
        entt::entity entity{};
        GeometryPropertyRef positions{};
        return CaptureScalarAnalysis(EditorProcessingCommandsAccess::Resolve(commands), request, backend, current, capture,
                                     entity, positions);
    }

    ScalarAnalysis PointCloudEditingService::StartScalarAnalysis(
        const ScalarAnalysisRequest& request, const StepRunExecution& execution, ScalarAnalysisRefusal& refusal)
    {
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        return StartScalarRun(EditorProcessingCommandsAccess::Resolve(commands),
            request,
            m_Impl->Workspace.Current(),
            m_Impl->ScalarRuns,
            execution,
            nullptr,
            {},
            refusal);
    }

    ScalarAnalysis PointCloudEditingService::StartScalarAnalysisForTest(const ScalarAnalysisRequest& request,
                                                                        std::vector<float> values,
                                                                        Graphics::GpuPropertyResidency& residency,
                                                                        ScalarAnalysisRefusal& refusal)
    {
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        return StartScalarRun(EditorProcessingCommandsAccess::Resolve(commands),
            request,
            m_Impl->Workspace.Current(),
            m_Impl->ScalarRuns,
            kOneShotStepRunExecution,
            &residency,
            std::move(values),
            refusal);
    }

    PointSession PointCloudEditingService::CurrentSession()
    {
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        return {.Active = EditorProcessingCommandsAccess::Resolve(commands).AttachmentActive,
                .Workspace = m_Impl->Workspace.Current()};
    }

    bool PointCloudEditingService::SessionCurrent(const PointSession& session) const
    {
        return session.Active && session.Active() && session.Workspace == m_Impl->Workspace.Current();
    }

    bool PointPropertyCombo(const char* label, GeometryPropertyRef& ref, const GeometryPropertyCatalogSnapshot& catalog)
    {
        const std::string current = ref.Name + " (" + std::string{ToString(ref.Domain)} + ")";
        if (!ImGui::BeginCombo(label, current.c_str())) return false;
        bool changed = false;
        for (const auto& entry : catalog.Entries)
        {
            const std::string name = entry.Ref.Name + " (" + std::string{ToString(entry.Ref.Domain)} + ")";
            if (ImGui::Selectable(name.c_str(), entry.Ref.Name == ref.Name && entry.Ref.Domain == ref.Domain))
            {
                changed = changed || !(entry.Ref == ref);
                ref = entry.Ref;
            }
        }
        ImGui::EndCombo();
        return changed;
    }

    bool PointScalarOutputInput(const char* label, GeometryPropertyRef& ref)
    {
        std::array<char, 256> name{};
        ref.Name.copy(name.data(), name.size() - 1);
        bool changed = false;
        if (ImGui::InputText(label, name.data(), name.size()))
        {
            ref.Name = name.data();
            changed = true;
        }
        using K = Geometry::PropertyValueKind;
        constexpr std::array kinds{K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double};
        int selected = -1;
        for (std::size_t i = 0; i < kinds.size(); ++i)
            if (ref.ValueKind == kinds[i]) selected = int(i);
        ImGui::PushID(label);
        if (ImGui::Combo("Storage", &selected, "Bool\0Int32\0UInt32\0UInt64\0Float\0Double\0") && selected >= 0)
        {
            ref.ValueKind = kinds[std::size_t(selected)];
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    PointCloudEditingModule::PointCloudEditingModule() = default;
    PointCloudEditingModule::~PointCloudEditingModule() = default;

    RuntimeModuleResult PointCloudEditingModule::OnRegister(EngineSetup& setup)
    {
        return setup.Services().Provide<PointCloudEditingService>(m_Service, Name());
    }

    RuntimeModuleResult PointCloudEditingModule::OnResolve(EngineSetup& setup)
    {
        m_Service.m_Impl->Workspace.AttachOwn(setup.Worlds(), setup.Services());
        return RuntimeModuleOk();
    }

    void PointCloudEditingModule::OnShutdown(RuntimeModuleShutdownContext&)
    {
        m_Service.m_Impl->Workspace.DetachOwn();
    }
}
