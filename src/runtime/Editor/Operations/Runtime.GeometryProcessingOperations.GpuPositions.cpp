// Accept of GPU-authored positions (RUNTIME-293, ADR 0030 decision 6): an accept-only
// transaction on the shared GPU transaction lifecycle, the undoable positions publication, and
// the 1:1 render commit that keeps the copied front in the block instead of uploading it again.
module;
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.System.BoundsPropagation;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    bool CapturePointPositionField(const GeometryEntityAvailability& a, GeometryPropertyRef& positions,
                                   PointPositionCapture& w, std::string& diagnostic)
    {
        if (!CapturePointInput(a, positions, true, w, diagnostic)) return false;
        w.Positions = positions;
        w.BeforeValues = ResolveGeometryPropertySet(a, positions.Domain)->Get<glm::vec3>(positions.Name).Vector();
        return true;
    }

    // Authored culling bounds follow the rows they describe. Only the local bounds are
    // history state; the world bounds are derived from the entity's current world matrix
    // at every mutation, so a transform edit between Accept and undo is never replayed.
    struct PointPositionBoundsState
    {
        std::optional<ECS::Components::Culling::Local::Bounds> Local{};
        // An entity authored with world bounds only (no local component): its world bounds
        // are the only truth to restore.
        std::optional<ECS::Components::Culling::World::Bounds> WorldOnly{};
    };
    enum class PointPositionBoundsOutcome : std::uint8_t { Absent, Valid, Failed };

    // The local bounds of the live rows (`slots`; deleted rows do not describe the shape).
    // Absent when the entity carries no authored culling bounds; Failed when the rows admit
    // no finite bounds (an overflowing extent, or no live row), which the caller rejects
    // before anything is mutated.
    PointPositionBoundsOutcome LocalBoundsFor(const entt::registry& registry, const entt::entity entity,
                                              const std::span<const glm::vec3> rows,
                                              const std::span<const std::uint32_t> slots,
                                              ECS::Components::Culling::Local::Bounds& out)
    {
        // Either authored component counts: local-only bounds would otherwise be propagated
        // into stale world bounds later.
        if (!registry.any_of<ECS::Components::Culling::World::Bounds, ECS::Components::Culling::Local::Bounds>(entity))
            return PointPositionBoundsOutcome::Absent;
        if (slots.empty() || rows.empty()) return PointPositionBoundsOutcome::Failed;
        glm::vec3 minimum = rows[slots[0]], maximum = minimum;
        for (const std::uint32_t slot : slots)
        {
            if (slot >= rows.size()) return PointPositionBoundsOutcome::Failed;
            minimum = glm::min(minimum, rows[slot]);
            maximum = glm::max(maximum, rows[slot]);
        }
        out.LocalBoundingAABB.Min = minimum;
        out.LocalBoundingAABB.Max = maximum;
        out.LocalBoundingSphere.Center = 0.5f * (minimum + maximum);
        out.LocalBoundingSphere.Radius = 0.5f * glm::length(maximum - minimum);
        const auto finite = [](const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (!finite(out.LocalBoundingSphere.Center) || !std::isfinite(out.LocalBoundingSphere.Radius))
            return PointPositionBoundsOutcome::Failed;
        return PointPositionBoundsOutcome::Valid;
    }

    bool WorldBoundsUnderCurrentMatrix(const entt::registry& registry, const entt::entity entity,
                                       const ECS::Components::Culling::Local::Bounds& local,
                                       ECS::Components::Culling::World::Bounds& out)
    {
        const auto* matrix = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
        return ECS::Systems::BoundsPropagation::TryComputeWorldBounds(local, matrix ? matrix->Matrix : glm::mat4{1.f}, out);
    }

    PointPositionBoundsState CurrentBoundsState(const entt::registry& registry, const entt::entity entity)
    {
        PointPositionBoundsState state{};
        if (const auto* local = registry.try_get<ECS::Components::Culling::Local::Bounds>(entity)) state.Local = *local;
        else if (const auto* world = registry.try_get<ECS::Components::Culling::World::Bounds>(entity)) state.WorldOnly = *world;
        return state;
    }

    // Applies a bounds state under the entity's current world matrix. False when the world
    // bounds cannot be derived (the caller treats the mutation as refused before it wrote).
    bool ApplyBoundsState(entt::registry& registry, const entt::entity entity, const PointPositionBoundsState& state)
    {
        if (state.Local)
        {
            ECS::Components::Culling::World::Bounds world{};
            if (!WorldBoundsUnderCurrentMatrix(registry, entity, *state.Local, world)) return false;
            registry.emplace_or_replace<ECS::Components::Culling::Local::Bounds>(entity, *state.Local);
            registry.emplace_or_replace<ECS::Components::Culling::World::Bounds>(entity, world);
        }
        else if (state.WorldOnly)
        {
            registry.remove<ECS::Components::Culling::Local::Bounds>(entity);
            registry.emplace_or_replace<ECS::Components::Culling::World::Bounds>(entity, *state.WorldOnly);
        }
        return true;
    }

    bool PointPositionBoundsValid(const entt::registry& registry, const entt::entity entity,
                                  const std::span<const glm::vec3> rows, const std::span<const std::uint32_t> slots,
                                  std::string& diagnostic)
    {
        ECS::Components::Culling::Local::Bounds local{};
        switch (LocalBoundsFor(registry, entity, rows, slots, local))
        {
        case PointPositionBoundsOutcome::Absent: return true;
        case PointPositionBoundsOutcome::Valid:
        {
            ECS::Components::Culling::World::Bounds world{};
            if (WorldBoundsUnderCurrentMatrix(registry, entity, local, world)) return true;
            break;
        }
        case PointPositionBoundsOutcome::Failed: break;
        }
        diagnostic = "The positions admit no finite culling bounds; previous positions retained.";
        return false;
    }

    bool PointPositionFieldCurrent(const EditorProcessingContext& context, const entt::entity entity,
                                   const PointPositionCapture& w)
    {
        return GeometryPropertiesCurrent(context, entity, w.Inputs) && EditorProcessingContextWorldCurrent(context);
    }

    EditorCommandHistoryStatus PublishPointPositionField(
        const EditorProcessingContext& context, const entt::entity entity, const PointPositionCapture& w,
        std::vector<glm::vec3> after, std::string label, std::function<bool(const PointPropertyWatch&)> commitOnce)
    {
        if (after.size() != w.BeforeValues.size()) return EditorCommandHistoryStatus::InvalidCommand;
        if (after == w.BeforeValues) return EditorCommandHistoryStatus::NoChange;
        struct State
        {
            std::vector<glm::vec3> Rows{};
            PointPositionBoundsState Bounds{}; // authored culling bounds of the rows (local truth)
        };
        const auto& registry = context.Scene->Raw();
        PointPositionBoundsState bounds{};
        {
            ECS::Components::Culling::Local::Bounds local{};
            switch (LocalBoundsFor(registry, entity, after, w.Slots, local))
            {
            case PointPositionBoundsOutcome::Absent: break;
            case PointPositionBoundsOutcome::Valid: bounds.Local = local; break;
            case PointPositionBoundsOutcome::Failed: return EditorCommandHistoryStatus::InvalidCommand;
            }
        }
        auto before = std::make_shared<State>(State{w.BeforeValues, CurrentBoundsState(registry, entity)});
        auto target = std::make_shared<State>(State{std::move(after), std::move(bounds)});
        const auto ref = w.Positions;
        // The positions are the output: their own revision is watched through `revisions`
        // (re-observed after every write), the other inputs through `inputs`.
        auto inputs = w.Inputs;
        std::erase_if(inputs, [&](const auto& input) { return input.Domain == ref.Domain && input.Name == ref.Name; });
        auto revisions = std::make_shared<std::array<PointPropertyWatch, 1>>();
        for (const auto& input : w.Inputs)
            if (input.Domain == ref.Domain && input.Name == ref.Name) revisions->front() = input;
        auto commit = std::make_shared<std::function<bool(const PointPropertyWatch&)>>(std::move(commitOnce));
        const auto mutate = [context, entity, inputs = std::move(inputs), revisions, ref, commit](const State& state) {
            const auto& values = state.Rows;
            if (!GeometryPropertiesCurrent(context, entity, inputs) ||
                !GeometryPropertiesCurrent(context, entity, *revisions) || !EditorProcessingContextWorldCurrent(context))
                return EditorCommandHistoryStatus::StaleEntity;
            auto& registry = context.Scene->Raw();
            auto* props = MutableGeometryProperties(registry, entity, ref.Domain);
            if (!props || props->Size() != values.size()) return EditorCommandHistoryStatus::StaleEntity;
            auto positions = props->Get<glm::vec3>(ref.Name);
            if (!positions) return EditorCommandHistoryStatus::InvalidCommand;
            // The culling bounds move with the rows in the same command (world bounds under
            // the matrix of this moment), so an accepted or restored result outside the old
            // bounds is not culled. Applied first: a refusal leaves the rows untouched.
            if (!ApplyBoundsState(registry, entity, state.Bounds)) return EditorCommandHistoryStatus::InvalidCommand;
            positions.Vector() = values;
            const auto now = BuildGeometryAvailability(registry, entity);
            *revisions = {ObserveGeometryProperty(now, ref.Domain, ref.Name)};
            // The accepting publication may leave the render block as it is (it already
            // holds the front); undo, redo and a refused commit upload the CPU rows once.
            bool acknowledged = false;
            if (*commit)
            {
                acknowledged = (*commit)(revisions->front());
                *commit = {};
            }
            if (!acknowledged) ECS::Components::DirtyTags::MarkVertexPositionsDirty(registry, entity);
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        return context.CommandHistory
            ? context.CommandHistory->Execute({.Label = std::move(label),
                                               .Redo = [mutate, target] { return mutate(*target); },
                                               .Undo = [mutate, before] { return mutate(*before); }}).Status
            : mutate(*target);
    }
}
}

namespace Extrinsic::Runtime
{
    namespace GP = GeometryProcessingDetail;

    // The accept-only transaction (no Run phase): an external producer writes the ring this run
    // acquired at Begin (ring 0 of the core, read back on Accept); Accept publishes the rows and
    // commits the accepted front to the render block.
    struct EditorGpuPositionRun : std::enable_shared_from_this<EditorGpuPositionRun>
    {
        GP::GpuTransactionCore Core{};
        entt::entity Entity{entt::null};
        std::uint32_t StableEntityId{};
        GP::PointPositionCapture Capture{};
        // The front Accept read back (buffer, publication and lease, held until the commit ran):
        // the render commit copies from it and BindRevision binds exactly that publication.
        std::optional<Graphics::GpuPropertyView> AcceptedFront{};
        std::vector<glm::vec3> TestFront{};
        std::string HistoryLabel{};
        EditorGpuPositionAcceptResult Result{};
        std::function<void(EditorGpuPositionAcceptResult)> Sink{};
    };

    namespace
    {
        using Run = EditorGpuPositionRunHandle;

        auto& Ring(const Run& w) { return w->Core.Rings[0]; }
        void Fail(const Run& w, const EditorCommandStatus status, std::string message)
        {
            GP::FinishGpuTransaction(w->Core, status == EditorCommandStatus::StaleEntity ? EditorGpuTransactionPhase::Discarded
                                                                                         : EditorGpuTransactionPhase::Failed,
                                     status, std::move(message));
        }

        // The readback landed: publish every row; for a 1:1 domain the render block keeps
        // the front, then the front becomes the canonical slot of the new revision.
        void Complete(const Run& w)
        {
            auto& t = w->Core;
            auto& residency = *t.Residency;
            const auto& ctx = t.Context;
            const auto& readback = Ring(w).Readback;
            const std::size_t rows = w->Capture.BeforeValues.size();
            std::vector<glm::vec3> after(rows);
            if (!w->TestFront.empty()) after = w->TestFront;
            else
            {
                if (!readback || readback->Failed || readback->Bytes.size() != rows * sizeof(glm::vec3))
                {
                    Fail(w, EditorCommandStatus::GeometryProcessingFailed,
                         "The GPU position readback failed; previous positions retained.");
                    return;
                }
                std::memcpy(after.data(), readback->Bytes.data(), rows * sizeof(glm::vec3));
            }
            for (const auto& p : after)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                {
                    Fail(w, EditorCommandStatus::GeometryProcessingFailed,
                         "The GPU positions are not finite; previous positions retained.");
                    return;
                }
            {
                std::string why;
                if (!GP::PointPositionBoundsValid(ctx.Scene->Raw(), w->Entity, after, w->Capture.Slots, why))
                {
                    Fail(w, EditorCommandStatus::GeometryProcessingFailed, std::move(why));
                    return;
                }
            }
            const std::span<const std::byte> bytes = std::as_bytes(std::span<const glm::vec3>{after});
            bool acknowledged = false;
            // Runs inside the accepting publication, after the rows were written.
            const auto commit = [w, &residency, bytes, &acknowledged](const GP::PointPropertyWatch& published) {
                // The front the readback leased, not whatever the ring shows now: a later
                // publication is neither committed to the block nor bound.
                const auto& front = w->AcceptedFront;
                const auto& ring = Ring(w);
                if (front && w->Core.Context.SpatialIndices && published.Revision)
                {
                    // A pending block copy holds the front's lease until its frame completed.
                    const auto status = w->Core.Context.SpatialIndices->CommitGpuPositions({
                        .StableEntityId = w->StableEntityId,
                        .PositionBytes = bytes,
                        .RowCount = front->Layout.Count,
                        .Revision = *published.Revision,
                        .Front = front->Buffer,
                        .FrontAddress = front->Address,
                        .FrontBytes = front->Bytes,
                        .FrontCount = front->Layout.Count,
                        .FrontStamp = GpuPropertyObservationStamp(*front),
                        .FrontLease = front->Lease});
                    acknowledged = status != SpatialIndexCache::GpuPositionCommitStatus::NotAcknowledged;
                }
                // ADR 0030 decision 6: the accepted publication is the canonical slot of the new
                // revision. A front published after the readback is not what the CPU holds:
                // the ring is discarded and the next GPU use uploads the revision once.
                if (!published.Revision || !front ||
                    !residency.BindRevision(ring.Key, *published.Revision, front->Publication))
                    (void)residency.Discard(ring.Key, ring.Generation); // only this run's ring, never a successor's
                return acknowledged;
            };
            // `bytes` spans `after`, which outlives the publication (passed by copy).
            const auto status = GP::PublishPointPositionField(ctx, w->Entity, w->Capture, after, w->HistoryLabel, commit);
            EditorGpuPositionAcceptResult result{.Status = EditorFeatureDetail::ToEditorCommandStatus(status),
                                                 .RenderAcknowledged = acknowledged};
            if (status == EditorCommandHistoryStatus::NoChange)
            {
                // The front equals the CPU rows: it serves the current revision as is; the
                // preview ends through the ordinary restore.
                const auto& ref = w->Capture.Positions;
                const auto watch = GP::ObserveGeometryProperty(BuildGeometryAvailability(ctx.Scene->Raw(), w->Entity),
                                                               ref.Domain, ref.Name);
                if (!watch.Revision || !w->AcceptedFront ||
                    !residency.BindRevision(Ring(w).Key, *watch.Revision, w->AcceptedFront->Publication))
                    (void)residency.Discard(Ring(w).Key, Ring(w).Generation);
                result.Message = "GPU positions equal the current positions.";
            }
            else if (status == EditorCommandHistoryStatus::Applied)
                result.Message = acknowledged ? "GPU positions applied; the render block keeps the accepted front."
                                              : "GPU positions applied.";
            else
                // Refused before the commit ran: the run ends and releases its ring.
                result.Message = status == EditorCommandHistoryStatus::StaleEntity
                    ? "The positions changed since the run; discard the result and run again."
                    : "GPU position publication rejected by history guards.";
            w->Result = result;
            if (status != EditorCommandHistoryStatus::Applied && status != EditorCommandHistoryStatus::NoChange)
            {
                Fail(w, result.Status, std::move(result.Message));
                return;
            }
            // The commit ran; a pending copy holds its own lease of the front.
            GP::FinishGpuTransaction(t, EditorGpuTransactionPhase::Applied, result.Status, std::move(result.Message));
        }
    }

    EditorGpuPositionRunHandle BeginEditorGpuPositionRun(const EditorProcessingCommands& commands, const std::uint32_t id,
                                                         GeometryPropertyRef positions, Graphics::GpuPropertyResidency& residency,
                                                         std::string& diagnostic)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.Scene || (context.AttachmentActive && !context.AttachmentActive()) ||
            !GP::EditorProcessingContextWorldCurrent(context))
        {
            diagnostic = "Workspace is unavailable.";
            return {};
        }
        const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
        if (!entity)
        {
            diagnostic = "Choose an existing geometry entity.";
            return {};
        }
        auto w = std::make_shared<EditorGpuPositionRun>();
        w->Entity = *entity;
        w->StableEntityId = id;
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        if (!GP::CapturePointPositionField(a, positions, w->Capture, diagnostic)) return {};
        auto& t = w->Core;
        t.Context = context;
        t.Residency = &residency;
        t.Label = "GPU positions";
        t.JobLabel = "GPU positions";
        t.AcceptJobName = "GPU positions accept";
        t.Identity = {.EntityId = id, .Scope = ToEditorJobScope(positions.Domain),
                      .OutputSemantic = GeometryPresentationSlotSemantic::Displacement, .OutputName = positions.Name};
        t.Rings[0] = {.Key = MakeGpuPropertyKey(context.World, *entity, positions), .ReadBack = true};
        t.RingCount = 1;
        auto& ring = t.Rings[0];
        if (residency.HasRing(ring.Key))
        {
            diagnostic = "A GPU result for these positions awaits Accept or Discard.";
            return {};
        }
        // The run owns the ring it creates: its first write slot is acquired here (depth 2:
        // fronts are copied into a render block, ADR 0030 decision 4).
        ring.Back = AcquireGpuPropertyOutput(residency, context.World, *entity, positions,
                                             std::uint32_t(w->Capture.BeforeValues.size()), 2u);
        if (!ring.Back)
        {
            // Nothing this Begin created may stay behind (the residency leaves no slot-less
            // ring; a ring here would be one created by this call).
            if (const auto created = residency.RingGeneration(ring.Key); created != 0u)
                (void)residency.Discard(ring.Key, created);
            diagnostic = "The GPU residency refused a write slot for the positions.";
            return {};
        }
        ring.Generation = residency.RingGeneration(ring.Key);
        auto* raw = w.get();
        const auto self = [raw] { return raw->shared_from_this(); };
        t.Hooks = {
            .Current = [raw] { return GP::PointPositionFieldCurrent(raw->Core.Context, raw->Entity, raw->Capture); },
            .CompleteAccept = [self] { Complete(self()); },
            .Accepting = [raw] { raw->Result = {.Status = EditorCommandStatus::Pending, .Message = "Reading the GPU positions back."}; },
            .Release = [raw] { raw->AcceptedFront.reset(); },
            .Deliver = [raw](const EditorCommandStatus status, std::string message) {
                raw->Result.Status = status;
                raw->Result.Message = std::move(message);
                // A terminal callback may capture this run: the sink leaves it before delivery.
                if (auto sink = std::move(raw->Sink)) sink(raw->Result);
            }};
        // Accept-only: the producer's fronts wait for Accept from the start.
        GP::ReadyGpuTransaction(t);
        return w;
    }

    std::optional<Graphics::GpuPropertyView> EditorGpuPositionRunFirstBack(const EditorGpuPositionRunHandle& run)
    {
        if (!run) return std::nullopt;
        return std::exchange(run->Core.Rings[0].Back, std::nullopt);
    }

    Graphics::GpuPropertyKey EditorGpuPositionRunKey(const EditorGpuPositionRunHandle& run) { return run->Core.Rings[0].Key; }

    std::uint32_t EditorGpuPositionRunRowCount(const EditorGpuPositionRunHandle& run)
    {
        return std::uint32_t(run->Capture.BeforeValues.size());
    }

    bool EditorGpuPositionRunCurrent(const EditorProcessingCommands&, const EditorGpuPositionRunHandle& run)
    {
        return run && GP::GpuTransactionCurrent(run->Core);
    }

    EditorGpuPositionAcceptResult AcceptEditorGpuPositionRun(
        const EditorProcessingCommands&, const EditorGpuPositionRunHandle& w, Graphics::GpuPropertyResidency&,
        std::string label, std::function<void(EditorGpuPositionAcceptResult)> onComplete,
        const std::span<const glm::vec3> frontForTest)
    {
        const auto refuse = [&](const EditorCommandStatus status, std::string message) {
            return EditorGpuPositionAcceptResult{.Status = status, .Message = std::move(message)};
        };
        if (!w) return refuse(EditorCommandStatus::InvalidProcessingParameters, "No GPU result waits for Accept.");
        if (auto refused = GP::GpuTransactionAcceptRefusal(w->Core, bool(onComplete)))
            return refuse(refused->Status, std::move(refused->Message));
        if (!frontForTest.empty() && frontForTest.size() != w->Capture.BeforeValues.size())
            return refuse(EditorCommandStatus::InvalidProcessingParameters, "The test front must cover every row.");
        auto& t = w->Core;
        if (onComplete) w->Sink = GuardEditorProcessingResult(t.Context, std::move(onComplete));
        w->TestFront.assign(frontForTest.begin(), frontForTest.end());
        t.TestFront = !w->TestFront.empty();
        w->HistoryLabel = std::move(label);
        auto& residency = *t.Residency;
        w->AcceptedFront = residency.HasRing(Ring(w).Key) ? residency.Front(Ring(w).Key) : std::nullopt;
        if (!w->AcceptedFront)
        {
            Fail(w, EditorCommandStatus::GeometryProcessingFailed, "The GPU result is no longer resident; previous positions retained.");
            return w->Result;
        }
        // The publication runs from a completion drain like every other editor result.
        (void)GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(w));
        return w->Result;
    }

    void DiscardEditorGpuPositionRun(const EditorProcessingCommands&, const EditorGpuPositionRunHandle& w,
                                     Graphics::GpuPropertyResidency&)
    {
        // Idempotent: a terminal run owns nothing any more (its ring was bound or released),
        // so a later run's ring on the same property is never touched. A readback in flight
        // records nothing (framed) or lands into a run that no longer publishes.
        if (w)
            GP::DiscardGpuTransaction(w->Core, EditorCommandStatus::StaleEntity,
                                      "GPU positions discarded; previous positions retained.");
    }
}
