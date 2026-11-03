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
#include <imgui.h>

module Extrinsic.Runtime.PointCloudEditing;

import Geometry.Properties;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.RHI.Device;
import Extrinsic.ECS.Component.DirtyTags;
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
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.VisualizationEditingOperations;
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
        std::vector<Detail::PointPropertyWatch> ScalarWatches{};
        std::vector<GeometryScalarPropertySnapshot> ScalarBefore{};
        Detail::PointRadiusRows RadiusPages{};
        std::shared_ptr<const SpatialIndexSnapshot> Index{};
        SpatialIndexHandle GpuIndex{};
        bool IndexReused{false};
        // GPU rows: the cached index first, then a private index over the moved points.
        SpatialIndexWorkspace Moving{};
        Detail::GpuRowPages Pages{};
        bool Moved{false};
        PointGpuRowStats Stats{};
        std::uint64_t PreviewRing{0u}; // the generation of the positions' ring PreviewPositions made; 0: none
    };

    struct PointCloudEditingService::Impl
    {
        EditorWorkspaceSlot Workspace{};

        // The capture's world is still the active one and its inputs and outputs are unchanged.
        static bool InputsCurrent(const PointEdit& edit)
        {
            const std::array outputs{edit.OutputWatch};
            return Detail::EditorProcessingContextWorldCurrent(edit.Context) &&
                Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, edit.Capture.Inputs) &&
                (!edit.Request.ScalarOutputs.empty() ||
                 Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, outputs)) &&
                Detail::GeometryPropertiesCurrent(edit.Context, edit.Entity, edit.ScalarWatches);
        }
    };

    PointCloudEditingService::PointCloudEditingService() : m_Impl(std::make_unique<Impl>()) {}
    PointCloudEditingService::~PointCloudEditingService() = default;

    void PointCloudEditingService::UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace.Use(workspace);
    }

    void PointCloudEditingService::ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace.Release(workspace);
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
        if (!Detail::CapturePointNormalInput(context, *entity, a, r.Positions, r.Normals, r.CopyValues, edit->Capture,
                                             diagnostic))
            return {};
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
                .HasZeroNormals = edit.Capture.HasZeroNormals,
                .Output = edit.Request.Output,
                .ScalarOutputs = edit.Request.ScalarOutputs};
    }

    bool PointCloudEditingService::Current(const PointEdit& edit) { return Impl::InputsCurrent(edit); }

    const Geometry::PointLBVH::Index* PointCloudEditingService::CpuIndex(const PointEdit& edit) const
    {
        return edit.Index ? &edit.Index->Index : nullptr;
    }

    bool PointCloudEditingService::GpuQueriesAvailable()
    {
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
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

    bool PointCloudEditingService::CpuRadiusRows(const PointEdit& edit, const float radius,
                                                 const std::uint32_t lowestIdLimit, PointNeighbourRows& rows,
                                                 std::string& diagnostic) const
    {
        if (!edit.Index)
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

    PointRowsState PointCloudEditingService::AdvanceGpuRadiusRows(PointEdit& edit, const float radius,
                                                                  const std::uint32_t batchSize,
                                                                  const std::uint32_t capacity,
                                                                  const std::uint32_t lowestIdLimit,
                                                                  PointNeighbourRows& rows, std::string& diagnostic)
    {
        auto& pages = edit.RadiusPages;
        if (edit.Context.SpatialIndices == nullptr)
        {
            diagnostic = "The spatial index cache is unavailable.";
            return PointRowsState::Failed;
        }
        if (!Impl::InputsCurrent(edit))
        {
            diagnostic = "The inputs changed.";
            pages.Batch.reset();
            return PointRowsState::Failed;
        }
        const auto state = Detail::AdvancePointRadiusRows(*edit.Context.SpatialIndices, edit.GpuIndex,
                                                          edit.Capture.Points, edit.Capture.Slots, radius, batchSize,
                                                          capacity, lowestIdLimit, pages, diagnostic);
        edit.Stats.QueryBatches = pages.QueryBatches;
        edit.Stats.Milliseconds = pages.Milliseconds;
        rows.MaximumNeighbors = pages.MaximumNeighbors;
        if (state == Detail::RowsState::Failed) return PointRowsState::Failed;
        if (state == Detail::RowsState::Pending) return PointRowsState::Pending;
        rows.Offsets = std::move(pages.Offsets);
        rows.Indices = std::move(pages.Indices);
        return PointRowsState::Ready;
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
        if (!Detail::EditorProcessingContextWorldCurrent(edit.Context) ||
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
