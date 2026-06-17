module;

#include <algorithm>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.SelectedMeshTextureBake;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.SelectionController;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = Extrinsic::ECS::Components::GeometrySources;

        struct MeshBakeSourceSnapshot
        {
            GS::Vertices Vertices{};
            GS::Halfedges Halfedges{};
            GS::Faces Faces{};

            [[nodiscard]] GS::ConstSourceView View() const noexcept
            {
                GS::ConstSourceView view{};
                view.ActiveDomain = GS::Domain::Mesh;
                view.VertexSource = &Vertices;
                view.HalfedgeSource = &Halfedges;
                view.FaceSource = &Faces;
                return view;
            }
        };

        struct SlotLookup
        {
            ProgressivePresentationBinding* Presentation{nullptr};
            ProgressiveSlotBinding* Slot{nullptr};
        };

        struct ConstSlotLookup
        {
            const ProgressivePresentationBinding* Presentation{nullptr};
            const ProgressiveSlotBinding* Slot{nullptr};
        };

        struct LoadedTexture
        {
            SelectedMeshTextureBakeStatus Status{SelectedMeshTextureBakeStatus::Success};
            Assets::AssetId Asset{};
        };

        [[nodiscard]] ECS::EntityHandle ResolveEntity(
            const ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !scene.Raw().valid(entity))
                return ECS::InvalidEntityHandle;
            return entity;
        }

        [[nodiscard]] bool IsSurfaceTextureSemantic(
            const ProgressiveSlotSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case ProgressiveSlotSemantic::Albedo:
            case ProgressiveSlotSemantic::Normal:
            case ProgressiveSlotSemantic::Roughness:
            case ProgressiveSlotSemantic::Metallic:
            case ProgressiveSlotSemantic::ScalarField:
            case ProgressiveSlotSemantic::Displacement:
                return true;
            case ProgressiveSlotSemantic::PointColor:
            case ProgressiveSlotSemantic::PointScalarField:
            case ProgressiveSlotSemantic::PointSize:
            case ProgressiveSlotSemantic::PointNormalOrientation:
            case ProgressiveSlotSemantic::LineColor:
            case ProgressiveSlotSemantic::LineScalarField:
            case ProgressiveSlotSemantic::LineWidth:
                return false;
            }
            return false;
        }

        [[nodiscard]] ProgressivePropertyValueKind DefaultExpectedKindForSemantic(
            const ProgressiveSlotSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case ProgressiveSlotSemantic::Normal:
                return ProgressivePropertyValueKind::Vec3;
            case ProgressiveSlotSemantic::Roughness:
            case ProgressiveSlotSemantic::Metallic:
            case ProgressiveSlotSemantic::ScalarField:
            case ProgressiveSlotSemantic::Displacement:
                return ProgressivePropertyValueKind::ScalarFloat;
            case ProgressiveSlotSemantic::Albedo:
                return ProgressivePropertyValueKind::Any;
            case ProgressiveSlotSemantic::PointColor:
            case ProgressiveSlotSemantic::PointScalarField:
            case ProgressiveSlotSemantic::PointSize:
            case ProgressiveSlotSemantic::PointNormalOrientation:
            case ProgressiveSlotSemantic::LineColor:
            case ProgressiveSlotSemantic::LineScalarField:
            case ProgressiveSlotSemantic::LineWidth:
                return ProgressivePropertyValueKind::Any;
            }
            return ProgressivePropertyValueKind::Any;
        }

        [[nodiscard]] MeshAttributeTextureBakeSourceDomain ToBakeDomain(
            const ProgressiveGeometryDomain domain,
            SelectedMeshTextureBakeStatus& status) noexcept
        {
            switch (domain)
            {
            case ProgressiveGeometryDomain::MeshVertex:
                status = SelectedMeshTextureBakeStatus::Success;
                return MeshAttributeTextureBakeSourceDomain::Vertex;
            case ProgressiveGeometryDomain::MeshFace:
                status = SelectedMeshTextureBakeStatus::Success;
                return MeshAttributeTextureBakeSourceDomain::Face;
            case ProgressiveGeometryDomain::Unknown:
            case ProgressiveGeometryDomain::MeshEdge:
            case ProgressiveGeometryDomain::MeshHalfedge:
            case ProgressiveGeometryDomain::MeshSurface:
            case ProgressiveGeometryDomain::GraphVertex:
            case ProgressiveGeometryDomain::GraphEdge:
            case ProgressiveGeometryDomain::Point:
                status = SelectedMeshTextureBakeStatus::UnsupportedSourceDomain;
                break;
            }
            return MeshAttributeTextureBakeSourceDomain::Vertex;
        }

        [[nodiscard]] MeshAttributeTextureBakeValueKind ToBakeValueKind(
            const ProgressivePropertyValueKind kind) noexcept
        {
            switch (kind)
            {
            case ProgressivePropertyValueKind::ScalarFloat:
            case ProgressivePropertyValueKind::ScalarDouble:
                return MeshAttributeTextureBakeValueKind::Scalar;
            case ProgressivePropertyValueKind::UInt32:
                return MeshAttributeTextureBakeValueKind::Label;
            case ProgressivePropertyValueKind::Vec2:
                return MeshAttributeTextureBakeValueKind::Vector2;
            case ProgressivePropertyValueKind::Vec3:
                return MeshAttributeTextureBakeValueKind::Vector3;
            case ProgressivePropertyValueKind::Vec4:
                return MeshAttributeTextureBakeValueKind::Vector4;
            case ProgressivePropertyValueKind::Any:
            case ProgressivePropertyValueKind::Unknown:
                break;
            }
            return MeshAttributeTextureBakeValueKind::Auto;
        }

        [[nodiscard]] bool EncoderCanHandle(
            const MeshAttributeTextureBakeEncoder encoder,
            const ProgressivePropertyValueKind kind) noexcept
        {
            if (encoder == MeshAttributeTextureBakeEncoder::Auto)
                return true;

            switch (encoder)
            {
            case MeshAttributeTextureBakeEncoder::LinearScalar:
            case MeshAttributeTextureBakeEncoder::ScalarColormap:
                return kind == ProgressivePropertyValueKind::ScalarFloat ||
                       kind == ProgressivePropertyValueKind::ScalarDouble;
            case MeshAttributeTextureBakeEncoder::LabelPalette:
                return kind == ProgressivePropertyValueKind::UInt32;
            case MeshAttributeTextureBakeEncoder::Vector2:
                return kind == ProgressivePropertyValueKind::Vec2;
            case MeshAttributeTextureBakeEncoder::Vector3:
            case MeshAttributeTextureBakeEncoder::Normal:
                return kind == ProgressivePropertyValueKind::Vec3;
            case MeshAttributeTextureBakeEncoder::RgbaColor:
                return kind == ProgressivePropertyValueKind::Vec3 ||
                       kind == ProgressivePropertyValueKind::Vec4;
            case MeshAttributeTextureBakeEncoder::Auto:
                break;
            }
            return false;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus StatusForResolution(
            const ProgressivePropertyResolutionStatus status) noexcept
        {
            switch (status)
            {
            case ProgressivePropertyResolutionStatus::Compatible:
                return SelectedMeshTextureBakeStatus::Success;
            case ProgressivePropertyResolutionStatus::MissingProperty:
                return SelectedMeshTextureBakeStatus::MissingProperty;
            case ProgressivePropertyResolutionStatus::TypeMismatch:
            case ProgressivePropertyResolutionStatus::UnsupportedType:
                return SelectedMeshTextureBakeStatus::UnsupportedPropertyType;
            case ProgressivePropertyResolutionStatus::CountMismatch:
                return SelectedMeshTextureBakeStatus::MismatchedPropertyCount;
            case ProgressivePropertyResolutionStatus::DomainUnavailable:
            case ProgressivePropertyResolutionStatus::UnsupportedDomain:
                return SelectedMeshTextureBakeStatus::UnsupportedSourceDomain;
            case ProgressivePropertyResolutionStatus::StaleGeneration:
                return SelectedMeshTextureBakeStatus::StaleCompletion;
            }
            return SelectedMeshTextureBakeStatus::UnsupportedPropertyType;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus StatusForBake(
            const MeshAttributeTextureBakeStatus status) noexcept
        {
            switch (status)
            {
            case MeshAttributeTextureBakeStatus::Success:
                return SelectedMeshTextureBakeStatus::Success;
            case MeshAttributeTextureBakeStatus::WrongDomain:
                return SelectedMeshTextureBakeStatus::NonMeshSelection;
            case MeshAttributeTextureBakeStatus::UnsupportedDomain:
                return SelectedMeshTextureBakeStatus::UnsupportedSourceDomain;
            case MeshAttributeTextureBakeStatus::MissingVertexSource:
            case MeshAttributeTextureBakeStatus::MissingHalfedgeTopology:
            case MeshAttributeTextureBakeStatus::MissingFaceTopology:
            case MeshAttributeTextureBakeStatus::EmptyMesh:
            case MeshAttributeTextureBakeStatus::InvalidTopology:
                return SelectedMeshTextureBakeStatus::BakeFailed;
            case MeshAttributeTextureBakeStatus::MissingTexcoords:
                return SelectedMeshTextureBakeStatus::MissingTexcoords;
            case MeshAttributeTextureBakeStatus::MissingProperty:
                return SelectedMeshTextureBakeStatus::MissingProperty;
            case MeshAttributeTextureBakeStatus::UnsupportedPropertyType:
                return SelectedMeshTextureBakeStatus::UnsupportedPropertyType;
            case MeshAttributeTextureBakeStatus::MismatchedPropertyCount:
                return SelectedMeshTextureBakeStatus::MismatchedPropertyCount;
            case MeshAttributeTextureBakeStatus::InvalidResolution:
                return SelectedMeshTextureBakeStatus::InvalidResolution;
            case MeshAttributeTextureBakeStatus::InvalidRange:
                return SelectedMeshTextureBakeStatus::InvalidRange;
            case MeshAttributeTextureBakeStatus::NonFiniteTexcoord:
                return SelectedMeshTextureBakeStatus::NonFiniteTexcoord;
            case MeshAttributeTextureBakeStatus::NonFinitePropertyValue:
                return SelectedMeshTextureBakeStatus::NonFinitePropertyValue;
            case MeshAttributeTextureBakeStatus::DegenerateAllTriangles:
                return SelectedMeshTextureBakeStatus::DegenerateAllTriangles;
            case MeshAttributeTextureBakeStatus::DegenerateUvTriangles:
                return SelectedMeshTextureBakeStatus::DegenerateUvTriangles;
            case MeshAttributeTextureBakeStatus::ZeroCoverageBake:
                return SelectedMeshTextureBakeStatus::ZeroCoverageBake;
            }
            return SelectedMeshTextureBakeStatus::BakeFailed;
        }

        [[nodiscard]] std::string BuildDiagnostic(
            const SelectedMeshTextureBakeStatus status)
        {
            return std::string{DebugNameForSelectedMeshTextureBakeStatus(status)};
        }

        [[nodiscard]] std::string BuildBakeDiagnostic(
            const MeshAttributeTextureBakeStatus status)
        {
            return std::string{DebugNameForMeshAttributeTextureBakeStatus(status)};
        }

        [[nodiscard]] std::string BuildSourceKey(
            const SelectedMeshTextureBakeRequest& request)
        {
            std::string key{"selected-mesh-"};
            key += std::to_string(request.StableEntityId);
            if (!request.GeneratedKey.empty())
            {
                key += "-";
                key += request.GeneratedKey;
            }
            return key;
        }

        [[nodiscard]] SelectedMeshTextureBakeBuildResult FailureBuild(
            const SelectedMeshTextureBakeStatus status,
            std::string diagnostic = {})
        {
            if (diagnostic.empty())
                diagnostic = BuildDiagnostic(status);
            return SelectedMeshTextureBakeBuildResult{
                .Status = status,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] SelectedMeshTextureBakeResult FailureResult(
            const SelectedMeshTextureBakeStatus status,
            std::string diagnostic = {})
        {
            if (diagnostic.empty())
                diagnostic = BuildDiagnostic(status);
            return SelectedMeshTextureBakeResult{
                .Status = status,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] const ProgressivePresentationBinding* ResolvePresentation(
            const ProgressivePresentationBindings& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            if (!request.TargetPresentationKey.empty())
                return FindPresentationBinding(bindings, request.TargetPresentationKey);

            if (const ProgressiveRenderLaneBinding* lane =
                    FindLaneBinding(bindings, request.TargetLane);
                lane != nullptr && !lane->PresentationKey.empty())
            {
                return FindPresentationBinding(bindings, lane->PresentationKey);
            }

            for (const ProgressivePresentationBinding& presentation :
                 bindings.Presentations)
            {
                if (FindSlotBinding(presentation, request.TargetSemantic) != nullptr)
                    return &presentation;
            }
            return nullptr;
        }

        [[nodiscard]] ProgressivePresentationBinding* ResolvePresentation(
            ProgressivePresentationBindings& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            if (!request.TargetPresentationKey.empty())
                return FindPresentationBinding(bindings, request.TargetPresentationKey);

            if (const ProgressiveRenderLaneBinding* lane =
                    FindLaneBinding(bindings, request.TargetLane);
                lane != nullptr && !lane->PresentationKey.empty())
            {
                return FindPresentationBinding(bindings, lane->PresentationKey);
            }

            for (ProgressivePresentationBinding& presentation : bindings.Presentations)
            {
                if (FindSlotBinding(presentation, request.TargetSemantic) != nullptr)
                    return &presentation;
            }
            return nullptr;
        }

        [[nodiscard]] ConstSlotLookup FindConstSlot(
            const ProgressivePresentationBindings& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            const ProgressivePresentationBinding* presentation =
                ResolvePresentation(bindings, request);
            if (presentation == nullptr)
                return {};
            return ConstSlotLookup{
                .Presentation = presentation,
                .Slot = FindSlotBinding(*presentation, request.TargetSemantic),
            };
        }

        [[nodiscard]] SlotLookup FindMutableSlot(
            ProgressivePresentationBindings& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            ProgressivePresentationBinding* presentation =
                ResolvePresentation(bindings, request);
            if (presentation == nullptr)
                return {};
            return SlotLookup{
                .Presentation = presentation,
                .Slot = FindSlotBinding(*presentation, request.TargetSemantic),
            };
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyProgressiveBindingsState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const ProgressivePresentationBindings& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            ECS::EntityHandle entity = ResolveEntity(*scene, stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;

            scene->Raw().emplace_or_replace<ProgressivePresentationBindings>(
                entity,
                state);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] bool HistorySucceeded(
            const EditorCommandHistoryStatus status) noexcept
        {
            switch (status)
            {
            case EditorCommandHistoryStatus::Applied:
            case EditorCommandHistoryStatus::Recorded:
            case EditorCommandHistoryStatus::Undone:
            case EditorCommandHistoryStatus::Redone:
            case EditorCommandHistoryStatus::NoChange:
                return true;
            case EditorCommandHistoryStatus::EmptyUndoStack:
            case EditorCommandHistoryStatus::EmptyRedoStack:
            case EditorCommandHistoryStatus::InvalidCommand:
            case EditorCommandHistoryStatus::CommandFailed:
            case EditorCommandHistoryStatus::UndoFailed:
            case EditorCommandHistoryStatus::RedoFailed:
            case EditorCommandHistoryStatus::StaleEntity:
            case EditorCommandHistoryStatus::MissingScene:
            case EditorCommandHistoryStatus::MissingSelectionController:
            case EditorCommandHistoryStatus::MissingTransform:
            case EditorCommandHistoryStatus::UnsupportedOperation:
                return false;
            }
            return false;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus CommitProgressiveChange(
            const SelectedMeshTextureBakeContext& context,
            const std::uint32_t stableEntityId,
            ProgressivePresentationBindings before,
            ProgressivePresentationBindings after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Bake Mesh Texture",
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyProgressiveBindingsState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyProgressiveBindingsState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return history.Succeeded()
                    ? SelectedMeshTextureBakeStatus::Success
                    : SelectedMeshTextureBakeStatus::CommandFailed;
            }

            const EditorCommandHistoryStatus status =
                ApplyProgressiveBindingsState(
                    context.Scene,
                    stableEntityId,
                    after);
            return HistorySucceeded(status)
                ? SelectedMeshTextureBakeStatus::Success
                : SelectedMeshTextureBakeStatus::CommandFailed;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus SetPendingBinding(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const SelectedMeshTextureBakeBuildResult& build,
            std::uint64_t& outBindingGeneration,
            bool& outPreviousOutputRetained)
        {
            if (!request.BindGeneratedTexture)
                return SelectedMeshTextureBakeStatus::Success;

            if (context.Scene == nullptr)
                return SelectedMeshTextureBakeStatus::MissingScene;
            const ECS::EntityHandle entity =
                ResolveEntity(*context.Scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return SelectedMeshTextureBakeStatus::StaleEntity;

            auto* current =
                context.Scene->Raw().try_get<ProgressivePresentationBindings>(entity);
            if (current == nullptr)
                return SelectedMeshTextureBakeStatus::MissingProgressiveBindings;

            ProgressivePresentationBindings before = *current;
            ProgressivePresentationBindings after = before;
            SlotLookup lookup = FindMutableSlot(after, request);
            if (lookup.Presentation == nullptr)
                return SelectedMeshTextureBakeStatus::MissingPresentation;
            if (lookup.Slot == nullptr)
                return SelectedMeshTextureBakeStatus::MissingSlot;

            ProgressiveSlotBinding& slot = *lookup.Slot;
            outPreviousOutputRetained = slot.GeneratedTexture.IsValid() ||
                                        slot.AuthoredTexture.IsValid();
            slot.SourceKind = ProgressiveSlotSourceKind::PropertyBake;
            slot.Property = ProgressivePropertyBindingDescriptor{
                .Domain = request.SourceDomain,
                .PropertyName = request.SourcePropertyName,
                .ExpectedValueKind = build.PropertyResolution.ActualValueKind,
                .ExpectedElementCount = build.ExpectedElementCount,
                .SourceGeneration = request.SourceGeneration,
            };
            slot.AuthoredTexture = {};
            slot.GeneratedPolicy = request.GeneratedPolicy;
            slot.Provenance =
                ProgressiveGeneratedOutputProvenance::PropertyBinding;
            slot.Readiness = ProgressiveReadinessState::Pending;
            slot.LastDiagnostic = "mesh texture bake pending";
            slot.Enabled = true;
            ++after.BindingGeneration;
            outBindingGeneration = after.BindingGeneration;

            return CommitProgressiveChange(
                context,
                request.StableEntityId,
                std::move(before),
                std::move(after));
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus SetReadyBindingDirect(
            ECS::Scene::Registry& scene,
            const SelectedMeshTextureBakeRequest& request,
            const Assets::AssetId generatedTexture,
            const SelectedMeshTextureBakeBuildResult& build,
            const std::string& diagnostic,
            std::uint64_t& outBindingGeneration)
        {
            const ECS::EntityHandle entity =
                ResolveEntity(scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return SelectedMeshTextureBakeStatus::StaleEntity;

            auto* bindings =
                scene.Raw().try_get<ProgressivePresentationBindings>(entity);
            if (bindings == nullptr)
                return SelectedMeshTextureBakeStatus::MissingProgressiveBindings;

            SlotLookup lookup = FindMutableSlot(*bindings, request);
            if (lookup.Presentation == nullptr)
                return SelectedMeshTextureBakeStatus::MissingPresentation;
            if (lookup.Slot == nullptr)
                return SelectedMeshTextureBakeStatus::MissingSlot;

            ProgressiveSlotBinding& slot = *lookup.Slot;
            slot.SourceKind = ProgressiveSlotSourceKind::GeneratedTextureAsset;
            slot.Property = ProgressivePropertyBindingDescriptor{
                .Domain = request.SourceDomain,
                .PropertyName = request.SourcePropertyName,
                .ExpectedValueKind = build.PropertyResolution.ActualValueKind,
                .ExpectedElementCount = build.ExpectedElementCount,
                .SourceGeneration = request.SourceGeneration,
            };
            slot.AuthoredTexture = {};
            slot.GeneratedTexture = generatedTexture;
            slot.GeneratedPolicy = request.GeneratedPolicy;
            slot.Provenance =
                ProgressiveGeneratedOutputProvenance::GeneratedTextureAsset;
            slot.Readiness = ProgressiveReadinessState::Ready;
            slot.LastDiagnostic = diagnostic;
            slot.Enabled = true;
            ++bindings->BindingGeneration;
            outBindingGeneration = bindings->BindingGeneration;
            return SelectedMeshTextureBakeStatus::Success;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus SetReadyBinding(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const Assets::AssetId generatedTexture,
            const SelectedMeshTextureBakeBuildResult& build,
            const std::string& diagnostic,
            std::uint64_t& outBindingGeneration)
        {
            if (!request.BindGeneratedTexture)
                return SelectedMeshTextureBakeStatus::Success;
            if (context.Scene == nullptr)
                return SelectedMeshTextureBakeStatus::MissingScene;

            const ECS::EntityHandle entity =
                ResolveEntity(*context.Scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return SelectedMeshTextureBakeStatus::StaleEntity;

            auto* current =
                context.Scene->Raw().try_get<ProgressivePresentationBindings>(entity);
            if (current == nullptr)
                return SelectedMeshTextureBakeStatus::MissingProgressiveBindings;

            ProgressivePresentationBindings before = *current;
            ProgressivePresentationBindings after = before;
            SlotLookup lookup = FindMutableSlot(after, request);
            if (lookup.Presentation == nullptr)
                return SelectedMeshTextureBakeStatus::MissingPresentation;
            if (lookup.Slot == nullptr)
                return SelectedMeshTextureBakeStatus::MissingSlot;

            ProgressiveSlotBinding& slot = *lookup.Slot;
            slot.SourceKind = ProgressiveSlotSourceKind::GeneratedTextureAsset;
            slot.Property = ProgressivePropertyBindingDescriptor{
                .Domain = request.SourceDomain,
                .PropertyName = request.SourcePropertyName,
                .ExpectedValueKind = build.PropertyResolution.ActualValueKind,
                .ExpectedElementCount = build.ExpectedElementCount,
                .SourceGeneration = request.SourceGeneration,
            };
            slot.AuthoredTexture = {};
            slot.GeneratedTexture = generatedTexture;
            slot.GeneratedPolicy = request.GeneratedPolicy;
            slot.Provenance =
                ProgressiveGeneratedOutputProvenance::GeneratedTextureAsset;
            slot.Readiness = ProgressiveReadinessState::Ready;
            slot.LastDiagnostic = diagnostic;
            slot.Enabled = true;
            ++after.BindingGeneration;
            outBindingGeneration = after.BindingGeneration;

            return CommitProgressiveChange(
                context,
                request.StableEntityId,
                std::move(before),
                std::move(after));
        }

        [[nodiscard]] LoadedTexture LoadOrReloadGeneratedTexture(
            Assets::AssetService& service,
            const Assets::AssetTexture2DPayload& payload,
            const std::string& path,
            const Assets::AssetId existing)
        {
            if (existing.IsValid() && service.IsAlive(existing))
            {
                const Core::Result reload =
                    service.Reload<Assets::AssetTexture2DPayload>(
                        existing,
                        [payload](std::string_view,
                                  Assets::AssetId)
                            -> Core::Expected<Assets::AssetTexture2DPayload>
                        {
                            return payload;
                        });
                if (!reload.has_value())
                {
                    return LoadedTexture{
                        .Status = SelectedMeshTextureBakeStatus::AssetLoadFailed,
                    };
                }
                return LoadedTexture{
                    .Status = SelectedMeshTextureBakeStatus::Success,
                    .Asset = existing,
                };
            }

            auto loaded = service.Load<Assets::AssetTexture2DPayload>(
                path,
                [payload](std::string_view,
                          Assets::AssetId)
                    -> Core::Expected<Assets::AssetTexture2DPayload>
                {
                    return payload;
                });
            if (!loaded.has_value())
            {
                return LoadedTexture{
                    .Status = SelectedMeshTextureBakeStatus::AssetLoadFailed,
                };
            }
            return LoadedTexture{
                .Status = SelectedMeshTextureBakeStatus::Success,
                .Asset = *loaded,
            };
        }

        [[nodiscard]] SelectedMeshTextureBakeResult ApplyBakePayload(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const SelectedMeshTextureBakeBuildResult& build,
            const MeshAttributeTextureBakeResult& bake,
            const bool useHistoryForReadyBinding)
        {
            if (context.AssetService == nullptr)
                return FailureResult(SelectedMeshTextureBakeStatus::MissingAssetService);

            if (bake.Status != MeshAttributeTextureBakeStatus::Success)
            {
                SelectedMeshTextureBakeResult result =
                    FailureResult(StatusForBake(bake.Status), BuildBakeDiagnostic(bake.Status));
                result.BakeStatus = bake.Status;
                result.BakeDiagnostics = bake.Diagnostics;
                result.GeneratedAssetPath = build.GeneratedAssetPath;
                return result;
            }

            const LoadedTexture loaded = LoadOrReloadGeneratedTexture(
                *context.AssetService,
                bake.Payload,
                build.GeneratedAssetPath,
                request.ExistingGeneratedTexture);
            if (loaded.Status != SelectedMeshTextureBakeStatus::Success)
            {
                return FailureResult(
                    loaded.Status,
                    "failed to load or reload generated texture payload");
            }

            std::uint64_t bindingGeneration = 0u;
            SelectedMeshTextureBakeStatus bindStatus =
                SelectedMeshTextureBakeStatus::Success;
            if (request.BindGeneratedTexture)
            {
                bindStatus = useHistoryForReadyBinding
                    ? SetReadyBinding(
                          context,
                          request,
                          loaded.Asset,
                          build,
                          "mesh texture bake ready",
                          bindingGeneration)
                    : SetReadyBindingDirect(
                          *context.Scene,
                          request,
                          loaded.Asset,
                          build,
                          "mesh texture bake ready",
                          bindingGeneration);
            }
            if (bindStatus != SelectedMeshTextureBakeStatus::Success)
            {
                return FailureResult(bindStatus);
            }

            return SelectedMeshTextureBakeResult{
                .Status = SelectedMeshTextureBakeStatus::Success,
                .BakeStatus = bake.Status,
                .BakeDiagnostics = bake.Diagnostics,
                .GeneratedTexture = loaded.Asset,
                .ExecutionMode = SelectedMeshTextureBakeExecutionMode::Synchronous,
                .BoundGeneratedTexture = request.BindGeneratedTexture,
                .BindingGeneration = bindingGeneration,
                .GeneratedAssetPath = build.GeneratedAssetPath,
                .Diagnostic = "mesh texture bake ready",
            };
        }

        [[nodiscard]] MeshBakeSourceSnapshot SnapshotMeshSources(
            const GS::ConstSourceView& view)
        {
            MeshBakeSourceSnapshot snapshot{};
            if (view.VertexSource != nullptr)
                snapshot.Vertices = *view.VertexSource;
            if (view.HalfedgeSource != nullptr)
                snapshot.Halfedges = *view.HalfedgeSource;
            if (view.FaceSource != nullptr)
                snapshot.Faces = *view.FaceSource;
            return snapshot;
        }

        [[nodiscard]] DerivedJobApplyValidation ValidateAsyncApply(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const std::uint64_t expectedBindingGeneration)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            const ECS::EntityHandle entity =
                ResolveEntity(*context.Scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return DerivedJobApplyValidation::MissingEntity;

            if (request.BindGeneratedTexture)
            {
                const auto* bindings =
                    context.Scene->Raw().try_get<ProgressivePresentationBindings>(entity);
                if (bindings == nullptr)
                    return DerivedJobApplyValidation::MissingEntity;
                if (bindings->BindingGeneration != expectedBindingGeneration)
                    return DerivedJobApplyValidation::StaleBindingGeneration;
            }

            return DerivedJobApplyValidation::Current;
        }
    }

    const char* DebugNameForSelectedMeshTextureBakeStatus(
        const SelectedMeshTextureBakeStatus status) noexcept
    {
        switch (status)
        {
        case SelectedMeshTextureBakeStatus::Success: return "SelectedMeshTextureBake.Success";
        case SelectedMeshTextureBakeStatus::Scheduled: return "SelectedMeshTextureBake.Scheduled";
        case SelectedMeshTextureBakeStatus::MissingScene: return "SelectedMeshTextureBake.MissingScene";
        case SelectedMeshTextureBakeStatus::MissingAssetService: return "SelectedMeshTextureBake.MissingAssetService";
        case SelectedMeshTextureBakeStatus::StaleEntity: return "SelectedMeshTextureBake.StaleEntity";
        case SelectedMeshTextureBakeStatus::NonMeshSelection: return "SelectedMeshTextureBake.NonMeshSelection";
        case SelectedMeshTextureBakeStatus::MissingProgressiveBindings: return "SelectedMeshTextureBake.MissingProgressiveBindings";
        case SelectedMeshTextureBakeStatus::MissingPresentation: return "SelectedMeshTextureBake.MissingPresentation";
        case SelectedMeshTextureBakeStatus::MissingSlot: return "SelectedMeshTextureBake.MissingSlot";
        case SelectedMeshTextureBakeStatus::UnsupportedSourceDomain: return "SelectedMeshTextureBake.UnsupportedSourceDomain";
        case SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic: return "SelectedMeshTextureBake.UnsupportedTargetSemantic";
        case SelectedMeshTextureBakeStatus::IncompatibleTargetSlot: return "SelectedMeshTextureBake.IncompatibleTargetSlot";
        case SelectedMeshTextureBakeStatus::InvalidResolution: return "SelectedMeshTextureBake.InvalidResolution";
        case SelectedMeshTextureBakeStatus::InvalidRange: return "SelectedMeshTextureBake.InvalidRange";
        case SelectedMeshTextureBakeStatus::MissingProperty: return "SelectedMeshTextureBake.MissingProperty";
        case SelectedMeshTextureBakeStatus::UnsupportedPropertyType: return "SelectedMeshTextureBake.UnsupportedPropertyType";
        case SelectedMeshTextureBakeStatus::MismatchedPropertyCount: return "SelectedMeshTextureBake.MismatchedPropertyCount";
        case SelectedMeshTextureBakeStatus::MissingTexcoords: return "SelectedMeshTextureBake.MissingTexcoords";
        case SelectedMeshTextureBakeStatus::NonFiniteTexcoord: return "SelectedMeshTextureBake.NonFiniteTexcoord";
        case SelectedMeshTextureBakeStatus::NonFinitePropertyValue: return "SelectedMeshTextureBake.NonFinitePropertyValue";
        case SelectedMeshTextureBakeStatus::DegenerateAllTriangles: return "SelectedMeshTextureBake.DegenerateAllTriangles";
        case SelectedMeshTextureBakeStatus::DegenerateUvTriangles: return "SelectedMeshTextureBake.DegenerateUvTriangles";
        case SelectedMeshTextureBakeStatus::ZeroCoverageBake: return "SelectedMeshTextureBake.ZeroCoverageBake";
        case SelectedMeshTextureBakeStatus::BakeFailed: return "SelectedMeshTextureBake.BakeFailed";
        case SelectedMeshTextureBakeStatus::AssetLoadFailed: return "SelectedMeshTextureBake.AssetLoadFailed";
        case SelectedMeshTextureBakeStatus::CommandFailed: return "SelectedMeshTextureBake.CommandFailed";
        case SelectedMeshTextureBakeStatus::JobSubmitFailed: return "SelectedMeshTextureBake.JobSubmitFailed";
        case SelectedMeshTextureBakeStatus::StaleCompletion: return "SelectedMeshTextureBake.StaleCompletion";
        }
        return "SelectedMeshTextureBake.Unknown";
    }

    SelectedMeshTextureBakeBuildResult BuildSelectedMeshTextureBakeRequest(
        const ECS::Scene::Registry& scene,
        const SelectedMeshTextureBakeRequest& request)
    {
        const ECS::EntityHandle entity =
            ResolveEntity(scene, request.StableEntityId);
        if (entity == ECS::InvalidEntityHandle)
            return FailureBuild(SelectedMeshTextureBakeStatus::StaleEntity);

        if (request.Width == 0u || request.Height == 0u)
            return FailureBuild(SelectedMeshTextureBakeStatus::InvalidResolution);
        if (request.SourcePropertyName.empty())
            return FailureBuild(SelectedMeshTextureBakeStatus::MissingProperty);
        if (!IsSurfaceTextureSemantic(request.TargetSemantic) ||
            request.TargetLane != ProgressiveRenderLane::Surface)
        {
            return FailureBuild(
                SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic);
        }

        const GS::ConstSourceView view = GS::BuildConstView(scene.Raw(), entity);
        if (view.ActiveDomain != GS::Domain::Mesh)
            return FailureBuild(SelectedMeshTextureBakeStatus::NonMeshSelection);

        SelectedMeshTextureBakeStatus domainStatus =
            SelectedMeshTextureBakeStatus::Success;
        const MeshAttributeTextureBakeSourceDomain bakeDomain =
            ToBakeDomain(request.SourceDomain, domainStatus);
        if (domainStatus != SelectedMeshTextureBakeStatus::Success)
            return FailureBuild(domainStatus);

        const std::size_t expectedCount =
            ResolvePropertyElementCount(view, request.SourceDomain);
        ProgressivePropertyValueKind expectedKind = request.ExpectedValueKind;
        if (expectedKind == ProgressivePropertyValueKind::Any)
            expectedKind = DefaultExpectedKindForSemantic(request.TargetSemantic);

        ProgressivePropertyBindingDescriptor descriptor{
            .Domain = request.SourceDomain,
            .PropertyName = request.SourcePropertyName,
            .ExpectedValueKind = expectedKind,
            .ExpectedElementCount = expectedCount,
            .SourceGeneration = request.SourceGeneration,
        };
        ProgressivePropertyResolution resolution =
            ResolvePropertyBinding(view, descriptor, request.SourceGeneration);
        if (!resolution.Compatible())
        {
            return FailureBuild(
                StatusForResolution(resolution.Status),
                resolution.Diagnostic);
        }
        if (!EncoderCanHandle(request.Encoder, resolution.ActualValueKind))
        {
            return FailureBuild(
                SelectedMeshTextureBakeStatus::IncompatibleTargetSlot,
                "encoder is incompatible with the selected property type");
        }

        if (request.BindGeneratedTexture)
        {
            const auto* bindings =
                scene.Raw().try_get<ProgressivePresentationBindings>(entity);
            if (bindings == nullptr)
                return FailureBuild(
                    SelectedMeshTextureBakeStatus::MissingProgressiveBindings);

            const ConstSlotLookup lookup = FindConstSlot(*bindings, request);
            if (lookup.Presentation == nullptr)
                return FailureBuild(SelectedMeshTextureBakeStatus::MissingPresentation);
            if (lookup.Slot == nullptr)
                return FailureBuild(SelectedMeshTextureBakeStatus::MissingSlot);
            if (lookup.Slot->Semantic != request.TargetSemantic)
                return FailureBuild(
                    SelectedMeshTextureBakeStatus::IncompatibleTargetSlot);
        }

        MeshAttributeTextureBakeRequest bake{};
        bake.SourcePropertyName = request.SourcePropertyName;
        bake.SourceDomain = bakeDomain;
        bake.ValueKind = request.ValueKind == MeshAttributeTextureBakeValueKind::Auto
            ? ToBakeValueKind(resolution.ActualValueKind)
            : request.ValueKind;
        bake.TargetSemantic = std::string{ToString(request.TargetSemantic)};
        bake.Encoder = request.Encoder;
        bake.TexcoordPropertyName = request.TexcoordPropertyName;
        bake.Width = request.Width;
        bake.Height = request.Height;
        bake.ColorSpace = request.ColorSpace;
        bake.PixelFormat = request.PixelFormat;
        bake.RangePolicy = request.RangePolicy;
        bake.RangeMin = request.RangeMin;
        bake.RangeMax = request.RangeMax;
        bake.DirtyStamp = request.DirtyStamp;
        bake.DebugName = request.SourcePropertyName;

        const std::string path =
            BuildMeshAttributeTextureBakeAssetPath(BuildSourceKey(request), bake);

        return SelectedMeshTextureBakeBuildResult{
            .Status = SelectedMeshTextureBakeStatus::Success,
            .BakeRequest = std::move(bake),
            .PropertyResolution = std::move(resolution),
            .ExpectedElementCount = expectedCount,
            .GeneratedAssetPath = path,
        };
    }

    SelectedMeshTextureBakeResult ApplySelectedMeshTextureBakeCommand(
        const SelectedMeshTextureBakeContext& context,
        const SelectedMeshTextureBakeRequest& request)
    {
        if (context.Scene == nullptr)
            return FailureResult(SelectedMeshTextureBakeStatus::MissingScene);
        if (context.AssetService == nullptr)
            return FailureResult(SelectedMeshTextureBakeStatus::MissingAssetService);

        SelectedMeshTextureBakeBuildResult build =
            BuildSelectedMeshTextureBakeRequest(*context.Scene, request);
        if (!build.Succeeded())
        {
            SelectedMeshTextureBakeResult result =
                FailureResult(build.Status, build.Diagnostic);
            result.GeneratedAssetPath = std::move(build.GeneratedAssetPath);
            return result;
        }

        const ECS::EntityHandle entity =
            ResolveEntity(*context.Scene, request.StableEntityId);
        if (entity == ECS::InvalidEntityHandle)
            return FailureResult(SelectedMeshTextureBakeStatus::StaleEntity);

        const GS::ConstSourceView view =
            GS::BuildConstView(context.Scene->Raw(), entity);

        const bool useDerived =
            request.PreferDerivedJob && context.DerivedJobs != nullptr;
        if (!useDerived)
        {
            const MeshAttributeTextureBakeResult bake =
                BakeMeshAttributeTexture(view, build.BakeRequest);
            return ApplyBakePayload(
                context,
                request,
                build,
                bake,
                true);
        }

        std::uint64_t bindingGeneration = 0u;
        bool previousOutputRetained = false;
        const SelectedMeshTextureBakeStatus pendingStatus =
            SetPendingBinding(
                context,
                request,
                build,
                bindingGeneration,
                previousOutputRetained);
        if (pendingStatus != SelectedMeshTextureBakeStatus::Success)
            return FailureResult(pendingStatus);

        MeshBakeSourceSnapshot snapshot = SnapshotMeshSources(view);
        auto bakeState = std::make_shared<std::optional<MeshAttributeTextureBakeResult>>();

        SelectedMeshTextureBakeContext applyContext = context;
        SelectedMeshTextureBakeRequest applyRequest = request;
        SelectedMeshTextureBakeBuildResult applyBuild = build;
        const std::uint64_t expectedBindingGeneration = bindingGeneration;
        DerivedJobDesc desc{};
        desc.Key = DerivedJobKey{
            .EntityId = request.StableEntityId,
            .Domain = request.SourceDomain,
            .OutputSemantic = request.TargetSemantic,
            .BindingGeneration = expectedBindingGeneration,
            .OutputName = request.SourcePropertyName,
        };
        desc.Name = "selected mesh texture bake";
        desc.RequestedJobDomain = ProgressiveJobDomain::Cpu;
        desc.EstimatedCost = std::max<std::uint32_t>(
            1u,
            request.Width * request.Height / 1024u);
        desc.HasPreviousOutput = previousOutputRetained;
        desc.Execute =
            [snapshot = std::move(snapshot),
             bakeRequest = build.BakeRequest,
             bakeState]() mutable -> DerivedJobWorkerResult
            {
                *bakeState = BakeMeshAttributeTexture(
                    snapshot.View(),
                    bakeRequest);
                const MeshAttributeTextureBakeResult& bake = **bakeState;
                if (bake.Status != MeshAttributeTextureBakeStatus::Success)
                {
                    return DerivedJobOutput{
                        .NormalizedProgress = 1.0f,
                        .ProgressDeterminate = true,
                        .Diagnostic = BuildBakeDiagnostic(bake.Status),
                    };
                }
                return DerivedJobOutput{
                    .PayloadToken =
                        static_cast<std::uint64_t>(bake.Payload.PixelBytes.size()),
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = "mesh texture bake ready",
                };
            };
        desc.ValidateOnMainThread =
            [applyContext, applyRequest, expectedBindingGeneration]()
            {
                return ValidateAsyncApply(
                    applyContext,
                    applyRequest,
                    expectedBindingGeneration);
            };
        desc.ApplyOnMainThread =
            [applyContext,
             applyRequest = std::move(applyRequest),
             applyBuild = std::move(applyBuild),
             bakeState](DerivedJobApplyContext&) mutable -> Core::Result
            {
                if (!bakeState->has_value())
                    return Core::Err(Core::ErrorCode::InvalidState);
                const MeshAttributeTextureBakeResult& bake = **bakeState;
                if (bake.Status != MeshAttributeTextureBakeStatus::Success)
                {
                    if (applyRequest.BindGeneratedTexture &&
                        applyContext.Scene != nullptr)
                    {
                        if (const ECS::EntityHandle entity =
                                ResolveEntity(
                                    *applyContext.Scene,
                                    applyRequest.StableEntityId);
                            entity != ECS::InvalidEntityHandle)
                        {
                            if (auto* bindings =
                                    applyContext.Scene->Raw()
                                        .try_get<ProgressivePresentationBindings>(
                                            entity);
                                bindings != nullptr)
                            {
                                if (SlotLookup lookup =
                                        FindMutableSlot(*bindings, applyRequest);
                                    lookup.Slot != nullptr)
                                {
                                    lookup.Slot->Readiness =
                                        ProgressiveReadinessState::Failed;
                                    lookup.Slot->LastDiagnostic =
                                        BuildBakeDiagnostic(bake.Status);
                                    ++bindings->BindingGeneration;
                                }
                            }
                        }
                    }
                    return Core::Err(Core::ErrorCode::InvalidArgument);
                }

                SelectedMeshTextureBakeResult applied =
                    ApplyBakePayload(
                        applyContext,
                        applyRequest,
                        applyBuild,
                        bake,
                        false);
                return applied.Succeeded()
                    ? Core::Ok()
                    : Core::Err(Core::ErrorCode::InvalidState);
            };

        const DerivedJobHandle handle = context.DerivedJobs->Submit(std::move(desc));
        if (!handle.IsValid())
            return FailureResult(SelectedMeshTextureBakeStatus::JobSubmitFailed);

        return SelectedMeshTextureBakeResult{
            .Status = SelectedMeshTextureBakeStatus::Scheduled,
            .GeneratedTexture = request.ExistingGeneratedTexture,
            .Job = handle,
            .ExecutionMode = SelectedMeshTextureBakeExecutionMode::DerivedJob,
            .BoundGeneratedTexture = request.BindGeneratedTexture,
            .PreviousOutputRetained = previousOutputRetained,
            .BindingGeneration = bindingGeneration,
            .GeneratedAssetPath = build.GeneratedAssetPath,
            .Diagnostic = "mesh texture bake scheduled",
        };
    }
}
