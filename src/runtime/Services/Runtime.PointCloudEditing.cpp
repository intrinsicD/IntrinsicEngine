module;

#include <algorithm>
#include <array>
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
#include <glm/vec3.hpp>

module Extrinsic.Runtime.PointCloudEditing;

import Geometry.Properties;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"

namespace Extrinsic::Runtime
{
    namespace Detail = GeometryProcessingDetail;
    using D = GeometryElementDomain;

    struct PointEdit
    {
        EditorProcessingContext Context{}; // a copy, as today's operations keep in their callbacks
        entt::entity Entity{};
        PointEditRequest Request{};
        Detail::PointNormalCapture Capture{};
        Detail::PointPropertyWatch OutputWatch{};
        std::vector<glm::vec3> BeforeOutput{}, AfterOutput{};
        std::shared_ptr<const SpatialIndexSnapshot> Index{};
        SpatialIndexHandle GpuIndex{};
        bool IndexReused{false};
        // GPU rows: the cached index first, then a private index over the moved points.
        SpatialIndexWorkspace Moving{};
        Detail::GpuRowPages Pages{};
        bool Moved{false};
        PointGpuRowStats Stats{};
    };

    struct PointCloudEditingService::Impl
    {
        EditorWorkspaceAttachment OwnWorkspace{};
        EditorWorkspaceAttachment* Workspace{nullptr};

        // The workspace's commands for this call; the own workspace prepares its frame here, the app's shell
        // prepares one every frame.
        EditorProcessingCommands Commands()
        {
            if (Workspace == nullptr) return {};
            if (Workspace == &OwnWorkspace && OwnWorkspace.IsAttached())
                (void)PrepareEditorWorkspaceSnapshotFrame(OwnWorkspace);
            return PrepareEditorProcessingCommands(*Workspace);
        }

        static bool InputsCurrent(const PointEdit& edit)
        {
            const std::array outputs{edit.OutputWatch};
            return Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, edit.Capture.Inputs) &&
                Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, outputs);
        }
    };

    PointCloudEditingService::PointCloudEditingService() : m_Impl(std::make_unique<Impl>()) {}
    PointCloudEditingService::~PointCloudEditingService() = default;

    void PointCloudEditingService::UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace = &workspace;
    }

    void PointCloudEditingService::ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept
    {
        auto& impl = *m_Impl;
        if (impl.Workspace == &workspace)
            impl.Workspace = impl.OwnWorkspace.IsAttached() ? &impl.OwnWorkspace : nullptr;
    }

    std::shared_ptr<PointEdit> PointCloudEditingService::Capture(const PointEditRequest& request,
                                                                  std::string& diagnostic)
    {
        const auto fail = [&](std::string why) -> std::shared_ptr<PointEdit>
        {
            diagnostic = std::move(why);
            return {};
        };
        const EditorProcessingCommands commands = m_Impl->Commands();
        const EditorProcessingContext& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.Scene == nullptr) return fail("Scene is unavailable.");
        const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), request.StableEntityId);
        if (!entity) return fail("The target entity is stale or missing.");
        auto edit = std::make_shared<PointEdit>();
        edit->Context = context;
        edit->Entity = *entity;
        edit->Request = request;
        auto& r = edit->Request;
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        if (r.Positions.Domain == D::Unknown) r.Positions.Domain = Detail::PrimaryPointDomain(a);
        if (r.Output.Domain == D::Unknown) r.Output.Domain = r.Positions.Domain;
        if (r.Normals.Domain == D::Unknown) r.Normals.Domain = r.Positions.Domain;
        const auto* props = ResolveGeometryPropertySet(a, r.Positions.Domain);
        if (props == nullptr || !ResolveGeometryProperty(a, r.Positions, props->Size(), false).Resolved())
            return fail("Choose a count-matched vec3 position property on a resolved element domain.");
        if (r.Output.Domain != r.Positions.Domain || (r.Output.Name == r.Normals.Name && r.Normals.Name != r.Positions.Name))
            return fail("The output must share the input domain and cannot overwrite a distinct normal input.");
        if (IsTopologyProperty(r.Output.Domain, r.Output.Name))
            return fail("The output cannot replace topology or deletion properties.");
        if (props->Exists(r.Output.Name) && !ResolveGeometryProperty(a, r.Output, props->Size(), false).Resolved())
            return fail("The output must be absent or a count-matched vec3 property.");
        if (!Detail::CapturePointNormalInput(context, *entity, a, r.Positions, r.Normals, r.CopyValues, edit->Capture,
                                             diagnostic))
            return {};
        edit->OutputWatch = Detail::ObserveGeometryProperty(a, r.Output.Domain, r.Output.Name);
        if (edit->Capture.LiveCount < r.MinimumLiveCount)
            return fail("At least " + std::to_string(r.MinimumLiveCount) + " live samples are required.");
        if (r.CopyValues)
        {
            if (edit->OutputWatch.Revision) edit->BeforeOutput = props->Get<glm::vec3>(r.Output.Name).Vector();
            edit->AfterOutput = edit->OutputWatch.Revision ? edit->BeforeOutput
                                                           : props->Get<glm::vec3>(r.Positions.Name).Vector();
        }
        if (r.AcquireIndex)
        {
            if (context.SpatialIndices == nullptr) return fail("The spatial index cache is unavailable.");
            const auto state = Detail::AcquirePointIndex(*context.SpatialIndices, context.World, *entity, r.Positions,
                                                         edit->Capture.Slots, edit->Capture.Points, edit->GpuIndex,
                                                         edit->Index, edit->IndexReused, diagnostic);
            if (state == Detail::PointIndexState::Unavailable) return {};
            if (state == Detail::PointIndexState::Mismatched)
                return fail("The index snapshot does not match the selected samples.");
        }
        return edit;
    }

    PointEditView PointCloudEditingService::View(const PointEdit& edit) const
    {
        return {.Points = edit.Capture.Points,
                .PointNormals = edit.Capture.Normals,
                .SlotCount = edit.Capture.SlotCount,
                .LiveCount = edit.Capture.LiveCount,
                .ValidLbvh = edit.Capture.ValidLbvh,
                .IndexReused = edit.IndexReused,
                .Output = edit.Request.Output};
    }

    bool PointCloudEditingService::Current(const PointEdit& edit) { return Impl::InputsCurrent(edit); }

    const Geometry::PointLBVH::Index* PointCloudEditingService::CpuIndex(const PointEdit& edit) const
    {
        return edit.Index ? &edit.Index->Index : nullptr;
    }

    bool PointCloudEditingService::GpuQueriesAvailable()
    {
        const EditorProcessingCommands commands = m_Impl->Commands();
        const EditorProcessingContext& context = EditorProcessingCommandsAccess::Resolve(commands);
        return context.JobCommands.Available() && context.SpatialIndices != nullptr &&
            context.SpatialIndices->GpuQueriesAvailable();
    }

    PointRowsState PointCloudEditingService::AdvanceGpuKnnRows(PointEdit& edit, const std::span<const glm::vec3> points,
                                                               const std::uint32_t width, const std::uint32_t batchSize,
                                                               std::vector<std::uint32_t>& rows, std::string& diagnostic)
    {
        auto* cache = edit.Context.SpatialIndices;
        const auto fail = [&](std::string why)
        {
            diagnostic = std::move(why);
            edit.Pages.Batch.reset();
            return PointRowsState::Failed;
        };
        if (cache == nullptr) return fail("The spatial index cache is unavailable.");
        if (!Impl::InputsCurrent(edit)) return fail("The inputs changed.");
        if (edit.Pages.Finished) return PointRowsState::Ready;
        if (!edit.GpuIndex.Value)
        {
            if (!edit.Moving.Ready() || !cache->UpdateWorkspace(edit.Moving, points))
            {
                edit.Moving = cache->CreateWorkspace(points);
                if (!edit.Moving.Ready()) return fail(edit.Moving.Diagnostic);
                ++edit.Stats.WorkspaceBuilds;
            }
            edit.GpuIndex = edit.Moving.Handle;
        }
        const auto& slots = edit.Capture.Slots;
        const auto state = Detail::AdvanceGpuRowPages(
            edit.Pages, points.size(), batchSize, diagnostic,
            [&](const SpatialNearestBatch& batch, std::string& why)
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
                        if (edit.Moved)
                        {
                            if (id >= points.size())
                            {
                                why = "Invalid private GPU neighbour row.";
                                return false;
                            }
                            rows.push_back(id);
                            continue;
                        }
                        // The cached index names source rows: map them to compact indices.
                        const auto found = std::lower_bound(slots.begin(), slots.end(), id);
                        if (found == slots.end() || *found != id)
                        {
                            why = "Invalid GPU source row.";
                            return false;
                        }
                        rows.push_back(std::uint32_t(found - slots.begin()));
                    }
                }
                return true;
            },
            [&](const std::size_t first, const std::size_t count, std::shared_ptr<SpatialNearestBatch> reuse)
            { return cache->QueueGpuKNearest(edit.GpuIndex, points.subspan(first, count), width, {}, std::move(reuse)); });
        edit.Stats.QueryBatches = edit.Pages.QueryBatches;
        if (state == Detail::RowsState::Failed) return fail(std::move(diagnostic));
        if (state == Detail::RowsState::Pending) return PointRowsState::Pending;
        edit.Stats.Milliseconds += edit.Pages.Milliseconds;
        return PointRowsState::Ready;
    }

    void PointCloudEditingService::RestartGpuKnnRows(PointEdit& edit)
    {
        // The next pass queries a private index over the moved points; the batch count stays cumulative.
        edit.GpuIndex = {};
        edit.Index.reset();
        edit.Pages.Restart();
        edit.Moved = true;
    }

    PointGpuRowStats PointCloudEditingService::GpuRowStats(const PointEdit& edit) const { return edit.Stats; }

    PointPublishStatus PointCloudEditingService::Publish(PointEdit& edit, const std::span<const glm::vec3> live,
                                                         std::string label)
    {
        // Staleness: checked here (the capture's revisions), then by the history entry on every redo and undo.
        const auto& slots = edit.Capture.Slots;
        if (live.size() != slots.size()) return PointPublishStatus::Rejected;
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
        if (!Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, inputs) ||
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

    GeometryPropertyCatalogSnapshot PointCloudEditingService::InputCatalog(const std::uint32_t stableEntityId,
                                                                          const std::size_t minimumLiveCount)
    {
        const EditorProcessingCommands commands = m_Impl->Commands();
        return Detail::BuildPointInputCatalog(EditorProcessingCommandsAccess::Resolve(commands), stableEntityId,
                                              minimumLiveCount);
    }

    PointCloudEditingModule::PointCloudEditingModule() = default;
    PointCloudEditingModule::~PointCloudEditingModule() = default;

    RuntimeModuleResult PointCloudEditingModule::OnRegister(EngineSetup& setup)
    {
        return setup.Services().Provide<PointCloudEditingService>(m_Service, Name());
    }

    RuntimeModuleResult PointCloudEditingModule::OnResolve(EngineSetup& setup)
    {
        auto& impl = *m_Service.m_Impl;
        if (impl.Workspace == nullptr)
        {
            impl.OwnWorkspace.Attach(setup.Worlds(), setup.Services());
            impl.Workspace = &impl.OwnWorkspace;
        }
        return RuntimeModuleOk();
    }

    void PointCloudEditingModule::OnShutdown(RuntimeModuleShutdownContext&)
    {
        auto& impl = *m_Service.m_Impl;
        impl.OwnWorkspace.Detach();
        impl.Workspace = nullptr;
    }
}
