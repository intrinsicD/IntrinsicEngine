// Point captures and session-owned input verdicts; mesh ring checks reuse the
// same revision, deferred-command and world/attachment lifetime guards.
#include <functional>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
// Named only so the shared job declarations in the point-field header resolve;
// both modules are already in this unit's closure through EditorProcessing.
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.PointLBVH;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.CommandBus;
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/internal/Runtime.EditorPointInputReadiness.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshReadiness.hpp"

namespace Extrinsic::Runtime
{
    extern "C++" struct EditorPointInputReadinessState
    {
        enum class InputKind { PointRows, MeshFaceRings, MeshCurvatureRows, MeshSegmentationRows };
        struct Entry
        {
            ECS::Scene::Registry* Scene{};
            WorldHandle World{};
            std::uint64_t WorldGeneration{};
            entt::entity Entity{};
            GeometryPropertyRef Positions{};
            InputKind Kind{};
            std::vector<GeometryPropertyRef> Bindings{};
            std::function<bool(const GeometryEntityAvailability&, std::string&)> Validate{};
            std::vector<GeometryProcessingDetail::PointPropertyWatch> Inputs{};
            std::function<bool()> AttachmentActive{};
            std::function<void()> Invalidate{};
            std::uint64_t RequestedFrame{};
            bool Queued{};
            std::optional<bool> Accepted{};
            std::size_t LiveCount{};
            bool ValidLbvh{true}, HasSubnormalCoordinates{}, HasZeroVectors{}, HasNonfiniteVectors{}, HasNonTriangleFaces{};
            float MinimumSquaredNorm{std::numeric_limits<float>::infinity()}, MaximumSquaredNorm{};
            std::string Diagnostic{};
        };
        WorldRegistry* Worlds{};
        CommandBus* Commands{};
        JobService* Jobs{};
        std::uint64_t Frame{};
        std::vector<std::shared_ptr<Entry>> Entries{};
        EditorPointInputReadinessStats Stats{};
    };
}

namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    namespace GS = ECS::Components::GeometrySources;
    using D = GeometryElementDomain;
    GeometryPropertyCatalogSnapshot BuildPointInputCandidateCatalog(
        const GeometryEntityAvailability& availability, std::uint32_t id)
    {
        std::uint64_t generation = 1469598103934665603ull;
        for (unsigned d = 1; d <= unsigned(D::PointCloudPoint); ++d)
        {
            const auto* props = ResolveGeometryPropertySet(availability, D(d));
            generation = (generation ^ (props ? props->Revision() : 0)) * 1099511628211ull;
        }
        auto catalog = BuildGeometryPropertyCatalogSnapshot(availability, id, generation);
        std::erase_if(catalog.Entries, [](const auto& entry) {
            return entry.Ref.ValueKind != Geometry::PropertyValueKind::Vec3;
        });
        for (auto& entry : catalog.Entries)
        {
            const auto* props = ResolveGeometryPropertySet(availability, entry.Ref.Domain);
            entry.PropertyGeneration = props->FindPropertyRevision(entry.Ref.Name).value_or(0);
        }
        return catalog;
    }
    GeometryPropertyCatalogSnapshot BuildPointInputCatalog(
        const EditorProcessingContext& context, std::uint32_t id, std::size_t minimumLiveCount)
    {
        if ((context.AttachmentActive && !context.AttachmentActive()) || !context.Scene) return {};
        const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
        if (!entity) return {};
        const auto availability = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        auto catalog = BuildPointInputCandidateCatalog(availability, id);
        std::erase_if(catalog.Entries, [&](auto& entry)
        {
            auto positions = entry.Ref;
            PointInputCapture capture;
            std::string diagnostic;
            return !PreparePointInput(context, *entity, availability, positions, capture, diagnostic) || capture.LiveCount < minimumLiveCount;
        });
        // Deferred membership can change without any source property edit.
        if (context.PointInputReadiness)
        {
            for (const auto& entry : catalog.Entries)
            {
                catalog.SourceGeneration = (catalog.SourceGeneration ^ unsigned(entry.Ref.Domain)) * 1099511628211ull;
                for (const unsigned char c : entry.Ref.Name)
                    catalog.SourceGeneration = (catalog.SourceGeneration ^ c) * 1099511628211ull;
                catalog.SourceGeneration = (catalog.SourceGeneration ^ entry.PropertyGeneration) * 1099511628211ull;
            }
            catalog.SourceGeneration = (catalog.SourceGeneration ^ catalog.Entries.size()) * 1099511628211ull;
        }
        return catalog;
    }
    PointPropertyWatch ObserveGeometryProperty(const GeometryEntityAvailability& a, D domain, std::string name)
    {
        const auto* props = ResolveGeometryPropertySet(a, domain);
        return {domain, name, props ? props->Size() : 0,
                props ? props->FindPropertyRevision(name) : std::nullopt};
    }
    Geometry::PropertySet *MutableGeometryProperties(entt::registry &raw, entt::entity entity, D domain)
    {
        auto view = GS::BuildMutableView(raw, entity);
        switch (domain)
        {
        case D::MeshVertex:
        case D::GraphNode:
        case D::PointCloudPoint:
            return view.VertexSource ? &view.VertexSource->Properties : nullptr;
        case D::MeshEdge:
        case D::GraphEdge:
            return view.EdgeSource ? &view.EdgeSource->Properties : nullptr;
        case D::MeshHalfedge:
        case D::GraphHalfedge:
            return view.HalfedgeSource ? &view.HalfedgeSource->Properties : nullptr;
        case D::MeshFace:
            return view.FaceSource ? &view.FaceSource->Properties : nullptr;
        default:
            return nullptr;
        }
    }
    D PrimaryPointDomain(const GeometryEntityAvailability &a)
    {
        for (auto d : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
            if (SupportsGeometryElementDomain(a, d))
                return d;
        return D::Unknown;
    }
    bool FinitePosition(glm::vec3 p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    bool GeometryPropertiesCurrent(const EditorProcessingContext& context, entt::entity entity,
                       std::span<const PointPropertyWatch> inputs)
    {
        if ((context.AttachmentActive && !context.AttachmentActive()) ||
            !context.Scene || !context.Scene->Raw().valid(entity)) return false;
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
        for (const auto& w : inputs)
            if (ObserveGeometryProperty(a, w.Domain, w.Name) != w) return false;
        return true;
    }
    bool ValidatePointOutputs(
        const GeometryEntityAvailability& a, const GeometryPropertyRef& positions,
        std::span<const GeometryPropertyRef> outputs, std::string_view outputLabel,
        std::string& diagnostic)
    {
        const auto fail = [&](std::string why) { diagnostic = std::move(why); return false; };
        const auto* props = ResolveGeometryPropertySet(a, positions.Domain);
        if (!props) return fail("Choose a resolved input domain.");
        for (const auto& output : outputs)
        {
            if (output.Domain != positions.Domain || output.Name == positions.Name)
                return fail(std::string(outputLabel) + " outputs must be distinct properties on the input domain.");
            if (IsTopologyProperty(output.Domain, output.Name)) return fail(std::string(outputLabel) + " outputs cannot replace topology/deletion properties.");
            if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                return fail(std::string(outputLabel) + " outputs must be absent or count-matched properties of the configured type.");
        }
        return true;
    }

    PointDeletionSource ResolvePointDeletionSource(D domain)
    {
        PointDeletionSource source{.Domain = domain};
        if (domain == D::MeshFace) source.Name = "f:deleted";
        if (domain == D::MeshEdge || domain == D::GraphEdge) source.Name = "e:deleted";
        if (domain == D::MeshHalfedge || domain == D::GraphHalfedge)
        {
            source.Domain = domain == D::MeshHalfedge ? D::MeshEdge : D::GraphEdge;
            source.Name = "e:deleted";
            source.Divisor = 2;
        }
        return source;
    }

    static bool CapturePointInputMetadata(
        const GeometryEntityAvailability& a, GeometryPropertyRef& positions,
        PointInputCapture& w, std::string& diagnostic)
    {
        const auto fail = [&](std::string why) { diagnostic = std::move(why); return false; };
        if (positions.Domain == D::Unknown) positions.Domain = PrimaryPointDomain(a);
        const auto* props = ResolveGeometryPropertySet(a, positions.Domain);
        if (!props || !ResolveGeometryProperty(a, positions, props->Size(), false).Resolved())
            return fail("Choose a count-matched vec3 position property on a resolved element domain.");
        const auto values = props->Get<glm::vec3>(positions.Name);
        if (!values || values.Size() != props->Size())
            return fail("Choose a count-matched vec3 position property on a resolved element domain.");
        if (props->Size() > std::numeric_limits<std::uint32_t>::max()) return fail("Input exceeds the supported slot range.");
        w.SlotCount = props->Size();
        w.Inputs.push_back(ObserveGeometryProperty(a, positions.Domain, positions.Name));
        const auto [deletionDomain, deletionName, divisor] = ResolvePointDeletionSource(positions.Domain);
        const auto* deletionProps = ResolveGeometryPropertySet(a, deletionDomain);
        if (!deletionProps || props->Size() % divisor || deletionProps->Size() != props->Size() / divisor)
            return fail("Invalid deletion domain/cardinality.");
        const auto deleted = deletionProps->Get<bool>(deletionName);
        if (deletionProps->Exists(deletionName) && (!deleted || deleted.Size() != deletionProps->Size()))
            return fail("Deletion mask must be a count-matched bool property.");
        w.Inputs.push_back(ObserveGeometryProperty(a, deletionDomain, deletionName));
        return true;
    }

    static bool ScanPointInputRows(
        const GeometryEntityAvailability& a, const GeometryPropertyRef& positions, bool copyValues,
        PointInputCapture& w, std::string& diagnostic)
    {
        const auto fail = [&](std::string why) { diagnostic = std::move(why); return false; };
        const auto* props = ResolveGeometryPropertySet(a, positions.Domain);
        const auto [deletionDomain, deletionName, divisor] = ResolvePointDeletionSource(positions.Domain);
        const auto deleted = ResolveGeometryPropertySet(a, deletionDomain)->Get<bool>(deletionName);
        const auto points = props->Get<glm::vec3>(positions.Name);
        for (std::uint32_t i = 0; i < props->Size(); ++i)
        {
            if (deleted && deleted[i / divisor]) continue;
            w.HasNonfiniteVectors = !FinitePosition(points[i]);
            if (w.HasNonfiniteVectors) return fail("Live position samples must be finite.");
            w.ValidLbvh &= Geometry::PointLBVH::ValidPoint(points[i]);
            // Bit inspection remains valid when a GPU flushes subnormal floats to zero.
            std::uint32_t vectorMagnitudeBits{};
            for (unsigned component = 0; component < 3; ++component)
            {
                const auto magnitude = std::bit_cast<std::uint32_t>(points[i][component]) & 0x7fffffffu;
                w.HasSubnormalCoordinates |= magnitude != 0 && magnitude < 0x00800000u;
                vectorMagnitudeBits |= magnitude;
            }
            w.HasZeroVectors |= vectorMagnitudeBits == 0;
            // Keep float overflow and underflow visible to the caller's normal policy.
            const auto squaredNorm = glm::dot(points[i], points[i]);
            w.MinimumSquaredNorm = std::min(w.MinimumSquaredNorm, squaredNorm);
            w.MaximumSquaredNorm = std::max(w.MaximumSquaredNorm, squaredNorm);
            ++w.LiveCount;
            if (copyValues) { w.Points.push_back(points[i]); w.Slots.push_back(i); }
        }
        return true;
    }
    bool CapturePointInput(
        const GeometryEntityAvailability& a, GeometryPropertyRef& positions, bool copyValues,
        PointInputCapture& w, std::string& diagnostic)
    {
        return CapturePointInputMetadata(a, positions, w, diagnostic) &&
               ScanPointInputRows(a, positions, copyValues, w, diagnostic);
    }

    using InputKind = EditorPointInputReadinessState::InputKind;

    static bool CaptureInputMetadata(
        InputKind kind, const GeometryEntityAvailability& a, GeometryPropertyRef& positions,
        PointInputCapture& capture, std::string& diagnostic)
    {
        if (kind == InputKind::PointRows)
            return CapturePointInputMetadata(a, positions, capture, diagnostic);
        if (MeshSupport::ValidateMeshSoupSourceMetadata(a.SourceView, diagnostic, positions.Name) !=
            EditorCommandStatus::Applied) return false;
        capture.Inputs = {
            ObserveGeometryProperty(a, D::MeshVertex, positions.Name),
            ObserveGeometryProperty(a, D::MeshHalfedge, std::string{GS::PropertyNames::kHalfedgeToVertex}),
            ObserveGeometryProperty(a, D::MeshHalfedge, std::string{GS::PropertyNames::kHalfedgeNext}),
            ObserveGeometryProperty(a, D::MeshHalfedge, std::string{GS::PropertyNames::kHalfedgeFace}),
            ObserveGeometryProperty(a, D::MeshFace, std::string{GS::PropertyNames::kFaceHalfedge})};
        // Ring validity depends on vertex slots, not coordinate values.
        capture.Inputs.front().Revision.reset();
        return true;
    }

    static bool ScanInputRows(
        InputKind kind, const GeometryEntityAvailability& a, const GeometryPropertyRef& positions,
        PointInputCapture& capture, std::string& diagnostic)
    {
        if (kind == InputKind::PointRows)
            return ScanPointInputRows(a, positions, false, capture, diagnostic);
        bool trianglesOnly = true;
        const bool valid = MeshSupport::ValidateMeshSoupFaceRings(
            a.SourceView, diagnostic, positions.Name, false, &trianglesOnly) == EditorCommandStatus::Applied;
        capture.HasNonTriangleFaces = !trianglesOnly;
        return valid;
    }

    namespace
    {
        struct CheckPointInputReadiness
        {
            std::weak_ptr<EditorPointInputReadinessState> State{};
            std::weak_ptr<EditorPointInputReadinessState::Entry> Entry{};
        };

        void CheckPointInput(const CheckPointInputReadiness& command)
        {
            const auto state = command.State.lock();
            const auto entry = command.Entry.lock();
            if (!state || !entry) return;
            entry->Queued = false;
            if (entry->AttachmentActive && !entry->AttachmentActive()) return;
            const auto invalidate = [&] { if (entry->Invalidate) entry->Invalidate(); };
            if (state->Worlds->ActiveWorld() != entry->World ||
                state->Worlds->Get(entry->World) != entry->Scene ||
                (state->Jobs && state->Jobs->WorldGeneration(entry->World) != entry->WorldGeneration) ||
                !entry->Scene->Raw().valid(entry->Entity))
            {
                invalidate();
                return;
            }
            const auto availability = BuildGeometryAvailability(entry->Scene->Raw(), entry->Entity);
            auto positions = entry->Positions;
            PointInputCapture capture;
            std::string diagnostic;
            const bool current = entry->Kind >= InputKind::MeshCurvatureRows
                ? std::ranges::all_of(entry->Inputs, [&](const auto& watch) {
                    return ObserveGeometryProperty(availability, watch.Domain, watch.Name) == watch; })
                : CaptureInputMetadata(entry->Kind, availability, positions, capture, diagnostic) &&
                  capture.Inputs == entry->Inputs;
            if (!current)
            {
                invalidate();
                return;
            }
            entry->Accepted = entry->Validate
                ? entry->Validate(availability, diagnostic)
                : ScanInputRows(entry->Kind, availability, positions, capture, diagnostic);
            ++state->Stats.PropertyScans;
            entry->LiveCount = capture.LiveCount;
            entry->ValidLbvh = capture.ValidLbvh;
            entry->HasSubnormalCoordinates = capture.HasSubnormalCoordinates;
            entry->HasZeroVectors = capture.HasZeroVectors;
            entry->HasNonfiniteVectors = capture.HasNonfiniteVectors;
            entry->HasNonTriangleFaces = capture.HasNonTriangleFaces;
            entry->MinimumSquaredNorm = capture.MinimumSquaredNorm;
            entry->MaximumSquaredNorm = capture.MaximumSquaredNorm;
            entry->Diagnostic = std::move(diagnostic);
            invalidate();
        }
    }

    bool EditorProcessingContextWorldCurrent(const EditorProcessingContext& context)
    {
        const auto& state = context.PointInputReadiness;
        return !state || (state->Worlds->ActiveWorld() == context.World &&
                         state->Worlds->Get(context.World) == context.Scene);
    }

    EditorPointInputReadinessStats PointInputReadinessStats(const EditorProcessingContext& context)
    {
        return context.PointInputReadiness ? context.PointInputReadiness->Stats : EditorPointInputReadinessStats{};
    }

    static bool PrepareInput(
        InputKind kind, const EditorProcessingContext& context, entt::entity entity,
        const GeometryEntityAvailability& availability, GeometryPropertyRef& positions,
        PointInputCapture& capture, std::string& diagnostic,
        std::vector<GeometryPropertyRef> bindings = {},
        std::function<bool(const GeometryEntityAvailability&, std::string&)> validate = {})
    {
        if (kind < InputKind::MeshCurvatureRows &&
            !CaptureInputMetadata(kind, availability, positions, capture, diagnostic)) return false;
        if (!context.PointInputReadiness)
            return validate ? validate(availability, diagnostic)
                            : ScanInputRows(kind, availability, positions, capture, diagnostic);
        auto& state = *context.PointInputReadiness;
        if (!state.Commands)
        {
            diagnostic = kind == InputKind::PointRows
                ? "Point-input readiness is unavailable. Attach an editor with a command queue."
                : "Mesh-ring readiness is unavailable. Attach an editor with a command queue.";
            return false;
        }
        const auto generation = state.Jobs ? state.Jobs->WorldGeneration(context.World) : 0;
        auto slot = std::ranges::find_if(state.Entries, [&](const auto& entry) {
            return entry->Scene == context.Scene && entry->World == context.World &&
                   entry->Entity == entity && entry->Positions == positions && entry->Kind == kind &&
                   entry->Bindings == bindings;
        });
        if (slot == state.Entries.end())
            slot = state.Entries.insert(slot, std::shared_ptr<EditorPointInputReadinessState::Entry>{});
        auto& entry = *slot;
        if (!entry || entry->WorldGeneration != generation || entry->Inputs != capture.Inputs)
            entry = std::make_shared<EditorPointInputReadinessState::Entry>(
                EditorPointInputReadinessState::Entry{
                    .Scene = context.Scene, .World = context.World, .WorldGeneration = generation,
                    .Entity = entity, .Positions = positions, .Kind = kind,
                    .Bindings = std::move(bindings), .Validate = std::move(validate), .Inputs = capture.Inputs,
                    .AttachmentActive = context.AttachmentActive,
                    .Invalidate = context.InvalidateWorkspaceSnapshotCache});
        entry->RequestedFrame = state.Frame;
        if (entry->Accepted.has_value())
        {
            capture.LiveCount = entry->LiveCount;
            capture.ValidLbvh = entry->ValidLbvh;
            capture.HasSubnormalCoordinates = entry->HasSubnormalCoordinates;
            capture.HasZeroVectors = entry->HasZeroVectors;
            capture.HasNonfiniteVectors = entry->HasNonfiniteVectors;
            capture.HasNonTriangleFaces = entry->HasNonTriangleFaces;
            capture.MinimumSquaredNorm = entry->MinimumSquaredNorm;
            capture.MaximumSquaredNorm = entry->MaximumSquaredNorm;
            diagnostic = entry->Diagnostic;
            return *entry->Accepted;
        }
        if (!entry->Queued)
        {
            entry->Queued = state.Commands->Enqueue(CheckPointInputReadiness{context.PointInputReadiness, entry}).IsValid();
            if (entry->Queued) ++state.Stats.ChecksQueued;
        }
        if (kind >= InputKind::MeshCurvatureRows)
        {
            diagnostic = entry->Queued
                ? "Checking bound mesh fields. Wait for input validation."
                : "Unable to queue mesh-field validation. Reattach the editor and retry.";
            return false;
        }
        diagnostic = entry->Queued
            ? (kind == InputKind::PointRows
                ? "Checking live point samples. Wait for input validation."
                : "Checking mesh face rings. Wait for input validation.")
            : (kind == InputKind::PointRows
                ? "Unable to queue point-input validation. Reattach the editor and retry."
                : "Unable to queue mesh-ring validation. Reattach the editor and retry.");
        return false;
    }

    bool PreparePointInput(
        const EditorProcessingContext& context, entt::entity entity,
        const GeometryEntityAvailability& availability, GeometryPropertyRef& positions,
        PointInputCapture& capture, std::string& diagnostic)
    {
        return PrepareInput(InputKind::PointRows, context, entity, availability, positions, capture, diagnostic);
    }

    bool MeshSupport::PrepareMeshSoupFaceRings(
        const EditorProcessingContext& context, entt::entity entity,
        const GeometryEntityAvailability& availability, std::string& diagnostic,
        GeometryPropertyRef positions, const bool requireTriangles)
    {
        PointInputCapture capture;
        if (!PrepareInput(InputKind::MeshFaceRings, context, entity, availability, positions, capture, diagnostic))
            return false;
        if (requireTriangles && capture.HasNonTriangleFaces)
        {
            diagnostic = "UV atlas generation requires triangular source faces; triangulate the mesh before generating an atlas.";
            return false;
        }
        return true;
    }

    bool PrepareMeshFieldInput(
        const EditorProcessingContext& context, entt::entity entity, const GeometryEntityAvailability& availability,
        const GeometryPropertyRef& positions, bool segmentation, std::vector<GeometryPropertyRef> bindings,
        std::vector<PointPropertyWatch> inputs,
        std::function<bool(const GeometryEntityAvailability&, std::string&)> validate, std::string& diagnostic)
    {
        PointInputCapture capture;
        capture.Inputs = std::move(inputs);
        auto boundPositions = positions;
        return PrepareInput(segmentation ? InputKind::MeshSegmentationRows : InputKind::MeshCurvatureRows, context, entity, availability, boundPositions,
                            capture, diagnostic, std::move(bindings), std::move(validate));
    }

    bool CapturePointNormalInput(
        const EditorProcessingContext& context, entt::entity entity,
        const GeometryEntityAvailability& a, GeometryPropertyRef& positions,
        GeometryPropertyRef& normals, bool copyValues, PointNormalCapture& w,
        std::string& diagnostic)
    {
        if (positions.Domain == D::Unknown) positions.Domain = PrimaryPointDomain(a);
        if (normals.Domain == D::Unknown) normals.Domain = positions.Domain;
        const auto* props = ResolveGeometryPropertySet(a, positions.Domain);
        if (!props || normals.Domain != positions.Domain ||
            !ResolveGeometryProperty(a, normals, props->Size(), false).Resolved())
        {
            diagnostic = "Choose count-matched vec3 normals on the position domain.";
            return false;
        }
        PointInputCapture normalInput;
        std::string normalDiagnostic;
        const auto capture = [&](GeometryPropertyRef& ref, PointInputCapture& input, std::string& why) {
            return copyValues ? CapturePointInput(a, ref, true, input, why)
                              : PreparePointInput(context, entity, a, ref, input, why);
        };
        // Request both properties in one frame; the cache shares each verdict
        // independently with other methods and discards superseded revisions.
        const bool positionsReady = capture(positions, w, diagnostic);
        const bool normalsReady = capture(normals, normalInput, normalDiagnostic);
        if (!positionsReady || !normalsReady)
        {
            if (positionsReady)
                diagnostic = normalInput.HasNonfiniteVectors
                    ? "Live normal samples must be finite." : std::move(normalDiagnostic);
            return false;
        }
        // Same-domain inputs use the same deletion mask and ascending row order.
        w.Inputs.push_back(ObserveGeometryProperty(a, normals.Domain, normals.Name));
        w.Normals = std::move(normalInput.Points);
        w.HasZeroNormals = normalInput.HasZeroVectors;
        w.MinimumNormalSquaredNorm = normalInput.MinimumSquaredNorm;
        w.MaximumNormalSquaredNorm = normalInput.MaximumSquaredNorm;
        return true;
    }

    bool CapturePointScalarField(
        const EditorProcessingContext& context, entt::entity entity,
        const GeometryEntityAvailability& a, GeometryPropertyRef& positions,
        GeometryPropertyRef& output, std::string_view outputLabel, bool copyValues,
        PointScalarCapture& w, std::string& diagnostic)
    {
        if (copyValues
            ? !CapturePointInput(a, positions, true, w, diagnostic)
            : !PreparePointInput(context, entity, a, positions, w, diagnostic)) return false;
        if (output.Domain == D::Unknown) output.Domain = positions.Domain;
        if (!ValidatePointOutputs(a, positions, std::span(&output, 1), outputLabel, diagnostic)) return false;
        w.OutputWatch = ObserveGeometryProperty(a, output.Domain, output.Name);
        w.Output = output;
        if (copyValues)
        {
            const auto* props = ResolveGeometryPropertySet(a, output.Domain);
            w.BeforeValues = CaptureGeometryScalarProperty(*props, output);
            w.AfterValues.resize(w.SlotCount);
        }
        return true;
    }

    bool PointScalarFieldCurrent(const EditorProcessingContext& context,
                                 entt::entity entity, const PointScalarCapture& w)
    {
        const std::array outputs{w.OutputWatch};
        return GeometryPropertiesCurrent(context, entity, w.Inputs) &&
               GeometryPropertiesCurrent(context, entity, outputs);
    }

    EditorCommandHistoryStatus PublishPointScalarField(
        const EditorProcessingContext& context, entt::entity entity,
        const PointScalarCapture& w, std::string label)
    {
        using State = GeometryScalarPropertySnapshot;
        auto before = std::make_shared<State>(w.BeforeValues);
        auto after = std::make_shared<State>(*before);
        if (!PrepareGeometryScalarProperty(*after, w.Output.ValueKind, w.SlotCount, w.Slots, w.AfterValues))
            return EditorCommandHistoryStatus::InvalidCommand;
        auto revisions = std::make_shared<std::array<PointPropertyWatch, 1>>(std::array{w.OutputWatch});
        const auto mutate = [context, entity, inputs=w.Inputs, ref=w.Output, revisions](const State& target)
        {
            if (!GeometryPropertiesCurrent(context, entity, inputs) ||
                !GeometryPropertiesCurrent(context, entity, *revisions))
                return EditorCommandHistoryStatus::StaleEntity;
            const auto& output = revisions->front();
            auto* props = MutableGeometryProperties(context.Scene->Raw(), entity, output.Domain);
            if (!ApplyGeometryScalarProperty(*props, ref, target))
                return EditorCommandHistoryStatus::InvalidCommand;
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
            *revisions = {ObserveGeometryProperty(a, output.Domain, output.Name)};
            // Scalar buffers follow their property revision. They do not alter
            // resident positions, topology or vertex channels.
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        return context.CommandHistory ? context.CommandHistory->Execute({.Label=std::move(label),
            .Redo=[mutate,after]{return mutate(*after);},
            .Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
    }


}

namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
        [[nodiscard]] bool IsFiniteGeometryPosition(
            const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>>
        CollectFiniteGeometryPositions(
            const Geometry::PropertySet& properties,
            const std::string_view positionProperty)
        {
            const auto positions =
                properties.Get<glm::vec3>(positionProperty);
            if (!positions || positions.Vector().empty())
                return std::nullopt;
            if (positions.Vector().size() != properties.Size())
                return std::nullopt;

            std::vector<glm::vec3> points{};
            points.reserve(positions.Vector().size());
            for (const glm::vec3& position : positions.Vector())
            {
                if (!IsFiniteGeometryPosition(position))
                    return std::nullopt;
                points.push_back(position);
            }
            return points;
        }

}

namespace Extrinsic::Runtime
{
    extern "C++" std::shared_ptr<EditorPointInputReadinessState> MakeEditorPointInputReadiness(
        WorldRegistry& worlds, CommandBus* commands, JobService* jobs)
    {
        auto state = std::make_shared<EditorPointInputReadinessState>();
        state->Worlds = &worlds;
        state->Commands = commands;
        state->Jobs = jobs;
        if (commands)
            commands->RegisterHandler<GeometryProcessingDetail::CheckPointInputReadiness>(
                [](CommandContext&, const GeometryProcessingDetail::CheckPointInputReadiness& command) {
                    GeometryProcessingDetail::CheckPointInput(command);
                    return CommandOutcome::Ok();
                });
        return state;
    }

    extern "C++" void BeginEditorPointInputReadinessFrame(EditorPointInputReadinessState& state)
    {
        ++state.Frame;
        // Keep only requests from this/previous prepared frame. Revision changes
        // replace a slot; closed panels cannot accumulate a history of source data.
        std::erase_if(state.Entries, [&](const auto& entry) {
            return entry->RequestedFrame + 1 < state.Frame;
        });
    }
}
