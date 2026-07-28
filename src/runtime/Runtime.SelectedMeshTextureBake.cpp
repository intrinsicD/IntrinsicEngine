module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.SelectedMeshTextureBake;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Properties;

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
            GeometryPresentationBindingRecipe* Presentation{nullptr};
            GeometryPresentationSlotRecipe* Slot{nullptr};
        };

        struct ConstSlotLookup
        {
            const GeometryPresentationBindingRecipe* Presentation{nullptr};
            const GeometryPresentationSlotRecipe* Slot{nullptr};
        };

        struct LoadedTexture
        {
            SelectedMeshTextureBakeStatus Status{SelectedMeshTextureBakeStatus::Success};
            Assets::AssetId Asset{};
        };

        struct SelectedObjectSpaceNormalBakeRequestBuild
        {
            SelectedMeshTextureBakeStatus Status{SelectedMeshTextureBakeStatus::Success};
            RuntimeObjectSpaceNormalBakeRequest Request{};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SelectedMeshTextureBakeStatus::Success;
            }
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

        [[nodiscard]] GeometryPropertyValueKindFilter DefaultExpectedKindForSemantic(
            const GeometryPresentationSlotSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case GeometryPresentationSlotSemantic::Normal:
                return Geometry::PropertyValueKind::Vec3;
            case GeometryPresentationSlotSemantic::Roughness:
            case GeometryPresentationSlotSemantic::Metallic:
            case GeometryPresentationSlotSemantic::ScalarField:
            case GeometryPresentationSlotSemantic::Displacement:
                return Geometry::PropertyValueKind::Float;
            case GeometryPresentationSlotSemantic::Albedo:
                return std::nullopt;
            case GeometryPresentationSlotSemantic::PointColor:
            case GeometryPresentationSlotSemantic::PointScalarField:
            case GeometryPresentationSlotSemantic::PointSize:
            case GeometryPresentationSlotSemantic::PointNormalOrientation:
            case GeometryPresentationSlotSemantic::LineColor:
            case GeometryPresentationSlotSemantic::LineScalarField:
            case GeometryPresentationSlotSemantic::LineWidth:
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] MeshAttributeTextureBakeSourceDomain ToBakeDomain(
            const GeometryElementDomain domain,
            SelectedMeshTextureBakeStatus& status) noexcept
        {
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
                status = SelectedMeshTextureBakeStatus::Success;
                return MeshAttributeTextureBakeSourceDomain::Vertex;
            case GeometryElementDomain::MeshFace:
                status = SelectedMeshTextureBakeStatus::Success;
                return MeshAttributeTextureBakeSourceDomain::Face;
            case GeometryElementDomain::MeshEdge:
                status = SelectedMeshTextureBakeStatus::Success;
                return MeshAttributeTextureBakeSourceDomain::Edge;
            case GeometryElementDomain::Unknown:
            case GeometryElementDomain::MeshHalfedge:
            case GeometryElementDomain::GraphNode:
            case GeometryElementDomain::GraphEdge:
            case GeometryElementDomain::PointCloudPoint:
                status = SelectedMeshTextureBakeStatus::UnsupportedSourceDomain;
                break;
            }
            return MeshAttributeTextureBakeSourceDomain::Vertex;
        }

        [[nodiscard]] MeshAttributeTextureBakeValueKind ToBakeValueKind(
            const Geometry::PropertyValueKind kind) noexcept
        {
            switch (kind)
            {
            case Geometry::PropertyValueKind::Float:
            case Geometry::PropertyValueKind::Double:
                return MeshAttributeTextureBakeValueKind::Scalar;
            case Geometry::PropertyValueKind::UInt32:
                return MeshAttributeTextureBakeValueKind::Label;
            case Geometry::PropertyValueKind::Vec2:
                return MeshAttributeTextureBakeValueKind::Vector2;
            case Geometry::PropertyValueKind::Vec3:
                return MeshAttributeTextureBakeValueKind::Vector3;
            case Geometry::PropertyValueKind::Vec4:
                return MeshAttributeTextureBakeValueKind::Vector4;
            case Geometry::PropertyValueKind::Unknown:
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
                break;
            }
            return MeshAttributeTextureBakeValueKind::Auto;
        }

        [[nodiscard]] bool EncoderCanHandle(
            const MeshAttributeTextureBakeEncoder encoder,
            const Geometry::PropertyValueKind kind) noexcept
        {
            if (encoder == MeshAttributeTextureBakeEncoder::Auto)
                return true;

            switch (encoder)
            {
            case MeshAttributeTextureBakeEncoder::LinearScalar:
            case MeshAttributeTextureBakeEncoder::ScalarColormap:
                return kind == Geometry::PropertyValueKind::Float ||
                       kind == Geometry::PropertyValueKind::Double;
            case MeshAttributeTextureBakeEncoder::LabelPalette:
                return kind == Geometry::PropertyValueKind::UInt32;
            case MeshAttributeTextureBakeEncoder::Vector2:
                return kind == Geometry::PropertyValueKind::Vec2;
            case MeshAttributeTextureBakeEncoder::Vector3:
            case MeshAttributeTextureBakeEncoder::Normal:
                return kind == Geometry::PropertyValueKind::Vec3;
            case MeshAttributeTextureBakeEncoder::RgbaColor:
                return kind == Geometry::PropertyValueKind::Vec3 ||
                       kind == Geometry::PropertyValueKind::Vec4;
            case MeshAttributeTextureBakeEncoder::Auto:
                break;
            }
            return false;
        }

        [[nodiscard]] bool IsScalarKind(const Geometry::PropertyValueKind kind) noexcept
        {
            return kind == Geometry::PropertyValueKind::Float ||
                   kind == Geometry::PropertyValueKind::Double;
        }

        [[nodiscard]] MeshAttributeTextureBakeEncoder ResolveMaterialSlotEncoder(
            const SelectedMeshTextureBakeRequest& request,
            const Geometry::PropertyValueKind kind) noexcept
        {
            if (request.Encoder != MeshAttributeTextureBakeEncoder::Auto)
                return request.Encoder;

            switch (request.TargetSemantic)
            {
            case GeometryPresentationSlotSemantic::Normal:
                return MeshAttributeTextureBakeEncoder::Normal;
            case GeometryPresentationSlotSemantic::Roughness:
            case GeometryPresentationSlotSemantic::Metallic:
            case GeometryPresentationSlotSemantic::Displacement:
                return MeshAttributeTextureBakeEncoder::LinearScalar;
            case GeometryPresentationSlotSemantic::ScalarField:
                return kind == Geometry::PropertyValueKind::UInt32
                    ? MeshAttributeTextureBakeEncoder::LabelPalette
                    : MeshAttributeTextureBakeEncoder::LinearScalar;
            case GeometryPresentationSlotSemantic::Albedo:
                switch (kind)
                {
                case Geometry::PropertyValueKind::Float:
                case Geometry::PropertyValueKind::Double:
                    return MeshAttributeTextureBakeEncoder::LinearScalar;
                case Geometry::PropertyValueKind::UInt32:
                    return MeshAttributeTextureBakeEncoder::LabelPalette;
                case Geometry::PropertyValueKind::Vec3:
                case Geometry::PropertyValueKind::Vec4:
                    return MeshAttributeTextureBakeEncoder::RgbaColor;
                case Geometry::PropertyValueKind::Vec2:
                    return MeshAttributeTextureBakeEncoder::Vector2;
                case Geometry::PropertyValueKind::Unknown:
                case Geometry::PropertyValueKind::Bool:
                case Geometry::PropertyValueKind::Int32:
                case Geometry::PropertyValueKind::UInt64:
                    break;
                }
                break;
            case GeometryPresentationSlotSemantic::PointColor:
            case GeometryPresentationSlotSemantic::PointScalarField:
            case GeometryPresentationSlotSemantic::PointSize:
            case GeometryPresentationSlotSemantic::PointNormalOrientation:
            case GeometryPresentationSlotSemantic::LineColor:
            case GeometryPresentationSlotSemantic::LineScalarField:
            case GeometryPresentationSlotSemantic::LineWidth:
                break;
            }

            return MeshAttributeTextureBakeEncoder::Auto;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus ValidateTargetSlotCompatibility(
            const GeometryPresentationSlotSemantic semantic,
            const Geometry::PropertyValueKind kind,
            const MeshAttributeTextureBakeEncoder encoder,
            std::string& diagnostic)
        {
            const auto incompatible =
                [&diagnostic](std::string message)
                {
                    diagnostic = std::move(message);
                    return SelectedMeshTextureBakeStatus::IncompatibleTargetSlot;
                };

            switch (semantic)
            {
            case GeometryPresentationSlotSemantic::Normal:
                if (kind == Geometry::PropertyValueKind::Vec3 &&
                    encoder == MeshAttributeTextureBakeEncoder::Normal)
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("normal material slot requires a vec3 property encoded as a normal texture");
            case GeometryPresentationSlotSemantic::Roughness:
            case GeometryPresentationSlotSemantic::Metallic:
                if (IsScalarKind(kind) &&
                    encoder == MeshAttributeTextureBakeEncoder::LinearScalar)
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("roughness and metallic material slots require scalar properties encoded as linear scalar textures");
            case GeometryPresentationSlotSemantic::Albedo:
                if ((IsScalarKind(kind) &&
                     (encoder == MeshAttributeTextureBakeEncoder::LinearScalar ||
                      encoder == MeshAttributeTextureBakeEncoder::ScalarColormap)) ||
                    (kind == Geometry::PropertyValueKind::UInt32 &&
                     encoder == MeshAttributeTextureBakeEncoder::LabelPalette) ||
                    ((kind == Geometry::PropertyValueKind::Vec3 ||
                      kind == Geometry::PropertyValueKind::Vec4) &&
                     encoder == MeshAttributeTextureBakeEncoder::RgbaColor))
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("albedo material slot requires a color, scalar colormap, scalar grayscale, or label-palette texture");
            case GeometryPresentationSlotSemantic::ScalarField:
                if ((IsScalarKind(kind) &&
                     (encoder == MeshAttributeTextureBakeEncoder::LinearScalar ||
                      encoder == MeshAttributeTextureBakeEncoder::ScalarColormap)) ||
                    (kind == Geometry::PropertyValueKind::UInt32 &&
                     encoder == MeshAttributeTextureBakeEncoder::LabelPalette))
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("scalar-field material slot requires scalar or label properties");
            case GeometryPresentationSlotSemantic::Displacement:
                diagnostic = "displacement bake output has no backend-resident material texture slot yet";
                return SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic;
            case GeometryPresentationSlotSemantic::PointColor:
            case GeometryPresentationSlotSemantic::PointScalarField:
            case GeometryPresentationSlotSemantic::PointSize:
            case GeometryPresentationSlotSemantic::PointNormalOrientation:
            case GeometryPresentationSlotSemantic::LineColor:
            case GeometryPresentationSlotSemantic::LineScalarField:
            case GeometryPresentationSlotSemantic::LineWidth:
                diagnostic = "target semantic is not a mesh surface material texture slot";
                return SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic;
            }

            return SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic;
        }

        [[nodiscard]] bool IsPackedMetallicRoughnessTarget(
            const GeometryPresentationSlotSemantic semantic) noexcept
        {
            return semantic == GeometryPresentationSlotSemantic::Roughness ||
                   semantic == GeometryPresentationSlotSemantic::Metallic;
        }

        [[nodiscard]] Assets::AssetTexture2DPayload AdaptPayloadForMaterialSlot(
            const SelectedMeshTextureBakeRequest& request,
            const Assets::AssetTexture2DPayload& payload)
        {
            if (!IsPackedMetallicRoughnessTarget(request.TargetSemantic) ||
                payload.Metadata.PixelFormat != Assets::AssetTexturePixelFormat::R8Unorm)
            {
                return payload;
            }

            Assets::AssetTexture2DPayload adapted = payload;
            const std::size_t pixelCount =
                static_cast<std::size_t>(payload.Metadata.Width) *
                static_cast<std::size_t>(payload.Metadata.Height);
            if (payload.PixelBytes.size() != pixelCount)
            {
                return adapted;
            }

            std::vector<std::byte> packed(pixelCount * 4u);
            for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
            {
                const std::byte scalar = payload.PixelBytes[pixel];
                const std::size_t offset = pixel * 4u;
                packed[offset + 0u] = std::byte{0xFF};
                packed[offset + 1u] = request.TargetSemantic == GeometryPresentationSlotSemantic::Roughness
                    ? scalar
                    : std::byte{0xFF};
                packed[offset + 2u] = request.TargetSemantic == GeometryPresentationSlotSemantic::Metallic
                    ? scalar
                    : std::byte{0x00};
                packed[offset + 3u] = std::byte{0xFF};
            }

            adapted.Metadata.Components = 4u;
            adapted.Metadata.PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm;
            adapted.Metadata.ColorSpace = Assets::AssetTextureColorSpace::Linear;
            adapted.PixelBytes = std::move(packed);
            return adapted;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus StatusForResolution(
            const GeometryPropertyResolutionStatus status) noexcept
        {
            switch (status)
            {
            case GeometryPropertyResolutionStatus::Resolved:
                return SelectedMeshTextureBakeStatus::Success;
            case GeometryPropertyResolutionStatus::MissingName:
            case GeometryPropertyResolutionStatus::MissingProperty:
                return SelectedMeshTextureBakeStatus::MissingProperty;
            case GeometryPropertyResolutionStatus::ValueKindMismatch:
                return SelectedMeshTextureBakeStatus::UnsupportedPropertyType;
            case GeometryPropertyResolutionStatus::ElementCountMismatch:
                return SelectedMeshTextureBakeStatus::MismatchedPropertyCount;
            case GeometryPropertyResolutionStatus::UnsupportedDomain:
                return SelectedMeshTextureBakeStatus::UnsupportedSourceDomain;
            case GeometryPropertyResolutionStatus::NonFiniteValues:
                return SelectedMeshTextureBakeStatus::NonFinitePropertyValue;
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

        [[nodiscard]] SelectedObjectSpaceNormalBakeRequestBuild
        FailureObjectSpaceNormalBakeRequestBuild(
            const SelectedMeshTextureBakeStatus status,
            std::string diagnostic = {})
        {
            if (diagnostic.empty())
                diagnostic = BuildDiagnostic(status);
            return SelectedObjectSpaceNormalBakeRequestBuild{
                .Status = status,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] SelectedObjectSpaceNormalBakeRequestBuild
        BuildSelectedMeshObjectSpaceNormalBakeRequest(
            const GS::ConstSourceView& view,
            const SelectedMeshTextureBakeRequest& request,
            RuntimeObjectSpaceNormalBakeTarget target)
        {
            if (request.StableEntityId == 0u ||
                view.VertexSource == nullptr)
            {
                return FailureObjectSpaceNormalBakeRequestBuild(
                    SelectedMeshTextureBakeStatus::StaleEntity);
            }

            const Geometry::ConstPropertySet vertexProperties{
                view.VertexSource->Properties};
            if (request.TexcoordPropertyName != "v:texcoord")
            {
                return FailureObjectSpaceNormalBakeRequestBuild(
                    SelectedMeshTextureBakeStatus::MissingTexcoords,
                    "selected mesh object-space normal bake must use the canonical v:texcoord channel");
            }
            const auto texcoords =
                vertexProperties.Get<glm::vec2>(request.TexcoordPropertyName);
            if (!texcoords.IsValid())
            {
                return FailureObjectSpaceNormalBakeRequestBuild(
                    SelectedMeshTextureBakeStatus::MissingTexcoords,
                    "selected mesh object-space normal bake requires resolved vertex texcoords");
            }
            const auto normals =
                vertexProperties.Get<glm::vec3>(request.SourcePropertyName);
            if (!normals.IsValid())
            {
                return FailureObjectSpaceNormalBakeRequestBuild(
                    SelectedMeshTextureBakeStatus::MissingProperty,
                    "selected mesh object-space normal bake requires a vec3 normal property");
            }

            Graphics::ObjectSpaceNormalTextureBakeOptions bakeOptions{};
            bakeOptions.Width = request.Width;
            bakeOptions.Height = request.Height;
            bakeOptions.Space = Graphics::NormalTextureSpace::ObjectSpaceNormal;

            VertexChannelBindingSet channelBindings{};
            const VertexChannelBindingSet* channelBindingsPtr = nullptr;
            if (request.SourcePropertyName != GS::PropertyNames::kNormal)
            {
                channelBindings.Normal.Enabled = true;
                channelBindings.Normal.SourceProperty =
                    request.SourcePropertyName;
                channelBindingsPtr = &channelBindings;
            }

            RuntimeObjectSpaceNormalBakeRequestBuildResult exact =
                BuildRuntimeObjectSpaceNormalBakeRequest(
                    view,
                    std::move(target),
                    bakeOptions,
                    channelBindingsPtr);
            if (!exact.Succeeded())
            {
                return FailureObjectSpaceNormalBakeRequestBuild(
                    SelectedMeshTextureBakeStatus::BakeFailed,
                    std::move(exact.Diagnostic));
            }

            return SelectedObjectSpaceNormalBakeRequestBuild{
                .Request = std::move(*exact.Request),
                .Diagnostic = std::move(exact.Diagnostic),
            };
        }

        [[nodiscard]] bool ShouldUseObjectSpaceNormalBakeQueue(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            return context.ObjectSpaceNormalBakeQueue != nullptr &&
                   request.BindGeneratedTexture &&
                   request.TargetLane == GeometryRenderLane::Surface &&
                   request.TargetSemantic == GeometryPresentationSlotSemantic::Normal &&
                   request.SourceDomain == GeometryElementDomain::MeshVertex;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus StatusForRuntimeObjectSpaceNormalBake(
            const RuntimeObjectSpaceNormalBakeStatus status) noexcept
        {
            switch (status)
            {
            case RuntimeObjectSpaceNormalBakeStatus::Queued:
                return SelectedMeshTextureBakeStatus::Scheduled;
            case RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding:
                return SelectedMeshTextureBakeStatus::Success;
            case RuntimeObjectSpaceNormalBakeStatus::NonOperationalBackend:
                return SelectedMeshTextureBakeStatus::NonOperationalBackend;
            case RuntimeObjectSpaceNormalBakeStatus::StaleCompletion:
                return SelectedMeshTextureBakeStatus::StaleCompletion;
            case RuntimeObjectSpaceNormalBakeStatus::UnsupportedNormalTextureSpace:
                return SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic;
            case RuntimeObjectSpaceNormalBakeStatus::InvalidRequest:
            case RuntimeObjectSpaceNormalBakeStatus::CapacityExceeded:
                return SelectedMeshTextureBakeStatus::CommandFailed;
            }
            return SelectedMeshTextureBakeStatus::CommandFailed;
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

        [[nodiscard]] const GeometryPresentationBindingRecipe* ResolvePresentation(
            const GeometryPresentationRecipe& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            if (!request.TargetPresentationKey.empty())
                return FindGeometryPresentationBinding(bindings, request.TargetPresentationKey);

            if (const GeometryPresentationLaneRecipe* lane =
                    FindGeometryPresentationLane(bindings, request.TargetLane);
                lane != nullptr && !lane->PresentationKey.empty())
            {
                return FindGeometryPresentationBinding(bindings, lane->PresentationKey);
            }

            for (const GeometryPresentationBindingRecipe& presentation :
                 bindings.Presentations)
            {
                if (FindGeometryPresentationSlot(presentation, request.TargetSemantic) != nullptr)
                    return &presentation;
            }
            return nullptr;
        }

        [[nodiscard]] GeometryPresentationBindingRecipe* ResolvePresentation(
            GeometryPresentationRecipe& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            if (!request.TargetPresentationKey.empty())
                return FindGeometryPresentationBinding(bindings, request.TargetPresentationKey);

            if (const GeometryPresentationLaneRecipe* lane =
                    FindGeometryPresentationLane(bindings, request.TargetLane);
                lane != nullptr && !lane->PresentationKey.empty())
            {
                return FindGeometryPresentationBinding(bindings, lane->PresentationKey);
            }

            for (GeometryPresentationBindingRecipe& presentation : bindings.Presentations)
            {
                if (FindGeometryPresentationSlot(presentation, request.TargetSemantic) != nullptr)
                    return &presentation;
            }
            return nullptr;
        }

        [[nodiscard]] ConstSlotLookup FindConstSlot(
            const GeometryPresentationRecipe& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            const GeometryPresentationBindingRecipe* presentation =
                ResolvePresentation(bindings, request);
            if (presentation == nullptr)
                return {};
            return ConstSlotLookup{
                .Presentation = presentation,
                .Slot = FindGeometryPresentationSlot(*presentation, request.TargetSemantic),
            };
        }

        [[nodiscard]] SlotLookup FindMutableSlot(
            GeometryPresentationRecipe& bindings,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            GeometryPresentationBindingRecipe* presentation =
                ResolvePresentation(bindings, request);
            if (presentation == nullptr)
                return {};
            return SlotLookup{
                .Presentation = presentation,
                .Slot = FindGeometryPresentationSlot(*presentation, request.TargetSemantic),
            };
        }

        void AdvancePresentationGeneration(
            std::uint64_t& generation) noexcept
        {
            ++generation;
            if (generation == 0u)
                generation = 1u;
        }

        [[nodiscard]] GeometryPresentationSlotStatus&
        EnsurePresentationSlotStatus(
            GeometryPresentationRuntimeState& state,
            const std::string_view presentationKey,
            const GeometryPresentationSlotSemantic semantic)
        {
            if (GeometryPresentationSlotStatus* status =
                    FindGeometryPresentationSlotStatus(
                        state, presentationKey, semantic))
            {
                return *status;
            }
            state.Slots.push_back(GeometryPresentationSlotStatus{
                .PresentationKey = std::string{presentationKey},
                .Semantic = semantic,
            });
            return state.Slots.back();
        }

        void ConfigurePropertyBakeRecipeSlot(
            GeometryPresentationSlotRecipe& slot,
            const SelectedMeshTextureBakeRequest& request,
            const SelectedMeshTextureBakeBuildResult& build)
        {
            slot.SourceKind = GeometryPresentationSourceKind::PropertyBake;
            slot.Property = GeometryPropertyRef{
                .Domain = request.SourceDomain,
                .Name = request.SourcePropertyName,
                .ValueKind = build.PropertyResolution.ResolvedValueKind,
            };
            slot.TextureAsset = {};
            slot.GeneratedPolicy = request.GeneratedPolicy;
            slot.Enabled = true;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyGeometryPresentationState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const GeometryPresentationRecipe& recipe,
            const GeometryPresentationRuntimeState& runtimeState)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            ECS::EntityHandle entity = ResolveEntity(*scene, stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;

            scene->Raw().emplace_or_replace<GeometryPresentationRecipe>(
                entity,
                recipe);
            scene->Raw().emplace_or_replace<GeometryPresentationRuntimeState>(
                entity,
                runtimeState);
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

        [[nodiscard]] SelectedMeshTextureBakeStatus CommitGeometryPresentationChange(
            const SelectedMeshTextureBakeContext& context,
            const std::uint32_t stableEntityId,
            GeometryPresentationRecipe beforeRecipe,
            GeometryPresentationRecipe afterRecipe,
            GeometryPresentationRuntimeState beforeState,
            GeometryPresentationRuntimeState afterState)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Bake Mesh Texture",
                            .Redo =
                                [scene, stableEntityId, afterRecipe, afterState]()
                                {
                                    return ApplyGeometryPresentationState(
                                        scene,
                                        stableEntityId,
                                        afterRecipe,
                                        afterState);
                                },
                            .Undo =
                                [scene, stableEntityId, beforeRecipe, beforeState]()
                                {
                                    return ApplyGeometryPresentationState(
                                        scene,
                                        stableEntityId,
                                        beforeRecipe,
                                        beforeState);
                                },
                            .Dirtying = true,
                        });
                return history.Succeeded()
                    ? SelectedMeshTextureBakeStatus::Success
                    : SelectedMeshTextureBakeStatus::CommandFailed;
            }

            const EditorCommandHistoryStatus status =
                ApplyGeometryPresentationState(
                    context.Scene,
                    stableEntityId,
                    afterRecipe,
                    afterState);
            return HistorySucceeded(status)
                ? SelectedMeshTextureBakeStatus::Success
                : SelectedMeshTextureBakeStatus::CommandFailed;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus SetPendingBinding(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const SelectedMeshTextureBakeBuildResult& build,
            std::uint64_t& outRecipeGeneration,
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

            auto& raw = context.Scene->Raw();
            auto* currentRecipe =
                raw.try_get<GeometryPresentationRecipe>(entity);
            if (currentRecipe == nullptr)
                return SelectedMeshTextureBakeStatus::MissingGeometryPresentationRecipe;
            auto* currentState =
                raw.try_get<GeometryPresentationRuntimeState>(entity);

            GeometryPresentationRecipe beforeRecipe = *currentRecipe;
            GeometryPresentationRecipe afterRecipe = beforeRecipe;
            GeometryPresentationRuntimeState beforeState =
                currentState != nullptr
                    ? *currentState
                    : GeometryPresentationRuntimeState{};
            GeometryPresentationRuntimeState afterState = beforeState;
            SlotLookup lookup = FindMutableSlot(afterRecipe, request);
            if (lookup.Presentation == nullptr)
                return SelectedMeshTextureBakeStatus::MissingPresentation;
            if (lookup.Slot == nullptr)
                return SelectedMeshTextureBakeStatus::MissingSlot;

            GeometryPresentationSlotRecipe& slot = *lookup.Slot;
            GeometryPresentationSlotStatus& status =
                EnsurePresentationSlotStatus(
                    afterState,
                    lookup.Presentation->Key,
                    request.TargetSemantic);
            outPreviousOutputRetained =
                status.GeneratedTexture.IsValid() ||
                slot.TextureAsset.IsValid();
            ConfigurePropertyBakeRecipeSlot(slot, request, build);
            status.SourceGeneration = request.SourceGeneration;
            status.Provenance =
                GeometryPresentationProvenance::PropertyBinding;
            status.Readiness = GeometryPresentationReadiness::Pending;
            status.Diagnostic = "mesh texture bake pending";
            AdvancePresentationGeneration(afterState.RecipeGeneration);
            outRecipeGeneration = afterState.RecipeGeneration;

            return CommitGeometryPresentationChange(
                context,
                request.StableEntityId,
                std::move(beforeRecipe),
                std::move(afterRecipe),
                std::move(beforeState),
                std::move(afterState));
        }

        [[nodiscard]] SelectedMeshTextureBakeResult
        ScheduleSelectedMeshObjectSpaceNormalBake(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const SelectedMeshTextureBakeBuildResult& build,
            const GS::ConstSourceView& view)
        {
            if (context.ObjectSpaceNormalBakeQueue == nullptr)
                return FailureResult(SelectedMeshTextureBakeStatus::CommandFailed);

            if (context.Scene == nullptr)
                return FailureResult(SelectedMeshTextureBakeStatus::MissingScene);
            const ECS::EntityHandle entity =
                ResolveEntity(*context.Scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return FailureResult(SelectedMeshTextureBakeStatus::StaleEntity);
            const auto* bindings =
                context.Scene->Raw().try_get<GeometryPresentationRecipe>(
                    entity);
            const auto* runtimeState =
                context.Scene->Raw()
                    .try_get<GeometryPresentationRuntimeState>(entity);
            if (bindings == nullptr || runtimeState == nullptr)
            {
                return FailureResult(
                    SelectedMeshTextureBakeStatus::MissingGeometryPresentationRecipe);
            }
            const GeometryPresentationBindingRecipe* presentation =
                ResolvePresentation(*bindings, request);
            if (presentation == nullptr || presentation->Key.empty())
            {
                return FailureResult(
                    SelectedMeshTextureBakeStatus::MissingPresentation);
            }
            std::uint64_t expectedRecipeGeneration =
                runtimeState->RecipeGeneration + 1u;
            if (expectedRecipeGeneration == 0u)
                expectedRecipeGeneration = 1u;

            SelectedObjectSpaceNormalBakeRequestBuild queueBuild =
                BuildSelectedMeshObjectSpaceNormalBakeRequest(
                    view,
                    request,
                    RuntimeObjectSpaceNormalBakeTarget{
                        .World = context.World,
                        .BindingEpoch = context.BindingEpoch,
                        .Entity = entity,
                        .StableEntityId = request.StableEntityId,
                        .PresentationKey = presentation->Key,
                        .Semantic = GeometryPresentationSlotSemantic::Normal,
                        .ExpectedRecipeGeneration =
                            expectedRecipeGeneration,
                    });
            if (!queueBuild.Succeeded())
            {
                SelectedMeshTextureBakeResult result =
                    FailureResult(queueBuild.Status, queueBuild.Diagnostic);
                result.GeneratedAssetPath = build.GeneratedAssetPath;
                return result;
            }

            if (context.ObjectSpaceNormalBakeDevice == nullptr ||
                !context.ObjectSpaceNormalBakeDevice->IsOperational())
            {
                RuntimeObjectSpaceNormalBakeResult queued =
                    context.ObjectSpaceNormalBakeQueue->Schedule(
                        queueBuild.Request,
                        false);
                SelectedMeshTextureBakeResult result =
                    FailureResult(
                        StatusForRuntimeObjectSpaceNormalBake(queued.Status),
                        queued.Diagnostic);
                result.ExecutionMode =
                    SelectedMeshTextureBakeExecutionMode::ObjectSpaceNormalBakeQueue;
                result.GeneratedAssetPath = build.GeneratedAssetPath;
                return result;
            }

            std::uint64_t recipeGeneration = 0u;
            bool previousOutputRetained = false;
            const SelectedMeshTextureBakeStatus pendingStatus =
                SetPendingBinding(
                    context,
                    request,
                    build,
                    recipeGeneration,
                    previousOutputRetained);
            if (pendingStatus != SelectedMeshTextureBakeStatus::Success)
                return FailureResult(pendingStatus);
            if (recipeGeneration != expectedRecipeGeneration)
            {
                return FailureResult(
                    SelectedMeshTextureBakeStatus::StaleCompletion,
                    "selected mesh presentation recipe generation changed while scheduling object-space normal bake");
            }

            RuntimeObjectSpaceNormalBakeResult queued =
                context.ObjectSpaceNormalBakeQueue->Schedule(
                    queueBuild.Request,
                    true);
            if (!queued.Succeeded())
            {
                SelectedMeshTextureBakeResult result =
                    FailureResult(
                        StatusForRuntimeObjectSpaceNormalBake(queued.Status),
                        queued.Diagnostic);
                result.ExecutionMode =
                    SelectedMeshTextureBakeExecutionMode::ObjectSpaceNormalBakeQueue;
                result.GeneratedAssetPath = build.GeneratedAssetPath;
                result.RecipeGeneration = recipeGeneration;
                result.PreviousOutputRetained = previousOutputRetained;
                return result;
            }

            return SelectedMeshTextureBakeResult{
                .Status = SelectedMeshTextureBakeStatus::Scheduled,
                .GeneratedTexture = {},
                .ExecutionMode =
                    SelectedMeshTextureBakeExecutionMode::ObjectSpaceNormalBakeQueue,
                .BoundGeneratedTexture = false,
                .PreviousOutputRetained = previousOutputRetained,
                .RecipeGeneration = recipeGeneration,
                .GeneratedAssetPath = build.GeneratedAssetPath,
                .Diagnostic = queued.Diagnostic,
            };
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus SetReadyBindingDirect(
            ECS::Scene::Registry& scene,
            const SelectedMeshTextureBakeRequest& request,
            const Assets::AssetId generatedTexture,
            const SelectedMeshTextureBakeBuildResult& build,
            const std::string& diagnostic,
            std::uint64_t& outRecipeGeneration)
        {
            const ECS::EntityHandle entity =
                ResolveEntity(scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return SelectedMeshTextureBakeStatus::StaleEntity;

            auto& raw = scene.Raw();
            auto* recipe =
                raw.try_get<GeometryPresentationRecipe>(entity);
            if (recipe == nullptr)
                return SelectedMeshTextureBakeStatus::MissingGeometryPresentationRecipe;

            SlotLookup lookup = FindMutableSlot(*recipe, request);
            if (lookup.Presentation == nullptr)
                return SelectedMeshTextureBakeStatus::MissingPresentation;
            if (lookup.Slot == nullptr)
                return SelectedMeshTextureBakeStatus::MissingSlot;

            GeometryPresentationSlotRecipe& slot = *lookup.Slot;
            ConfigurePropertyBakeRecipeSlot(slot, request, build);
            auto* runtimeState =
                raw.try_get<GeometryPresentationRuntimeState>(entity);
            if (runtimeState == nullptr)
            {
                runtimeState = &raw.emplace<GeometryPresentationRuntimeState>(
                    entity);
            }
            GeometryPresentationSlotStatus& status =
                EnsurePresentationSlotStatus(
                    *runtimeState,
                    lookup.Presentation->Key,
                    request.TargetSemantic);
            status.GeneratedTexture = generatedTexture;
            status.SourceGeneration = request.SourceGeneration;
            status.Provenance =
                GeometryPresentationProvenance::GeneratedTextureAsset;
            status.Readiness = GeometryPresentationReadiness::Ready;
            status.Diagnostic = diagnostic;
            AdvancePresentationGeneration(status.OutputGeneration);
            outRecipeGeneration = runtimeState->RecipeGeneration;
            return SelectedMeshTextureBakeStatus::Success;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus SetReadyBinding(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const Assets::AssetId generatedTexture,
            const SelectedMeshTextureBakeBuildResult& build,
            const std::string& diagnostic,
            std::uint64_t& outRecipeGeneration)
        {
            if (!request.BindGeneratedTexture)
                return SelectedMeshTextureBakeStatus::Success;
            if (context.Scene == nullptr)
                return SelectedMeshTextureBakeStatus::MissingScene;

            const ECS::EntityHandle entity =
                ResolveEntity(*context.Scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return SelectedMeshTextureBakeStatus::StaleEntity;

            auto& raw = context.Scene->Raw();
            auto* currentRecipe =
                raw.try_get<GeometryPresentationRecipe>(entity);
            if (currentRecipe == nullptr)
                return SelectedMeshTextureBakeStatus::MissingGeometryPresentationRecipe;
            auto* currentState =
                raw.try_get<GeometryPresentationRuntimeState>(entity);

            GeometryPresentationRecipe beforeRecipe = *currentRecipe;
            GeometryPresentationRecipe afterRecipe = beforeRecipe;
            GeometryPresentationRuntimeState beforeState =
                currentState != nullptr
                    ? *currentState
                    : GeometryPresentationRuntimeState{};
            GeometryPresentationRuntimeState afterState = beforeState;
            SlotLookup lookup = FindMutableSlot(afterRecipe, request);
            if (lookup.Presentation == nullptr)
                return SelectedMeshTextureBakeStatus::MissingPresentation;
            if (lookup.Slot == nullptr)
                return SelectedMeshTextureBakeStatus::MissingSlot;

            GeometryPresentationSlotRecipe& slot = *lookup.Slot;
            ConfigurePropertyBakeRecipeSlot(slot, request, build);
            GeometryPresentationSlotStatus& status =
                EnsurePresentationSlotStatus(
                    afterState,
                    lookup.Presentation->Key,
                    request.TargetSemantic);
            status.GeneratedTexture = generatedTexture;
            status.SourceGeneration = request.SourceGeneration;
            status.Provenance =
                GeometryPresentationProvenance::GeneratedTextureAsset;
            status.Readiness = GeometryPresentationReadiness::Ready;
            status.Diagnostic = diagnostic;
            AdvancePresentationGeneration(status.OutputGeneration);
            AdvancePresentationGeneration(afterState.RecipeGeneration);
            outRecipeGeneration = afterState.RecipeGeneration;

            return CommitGeometryPresentationChange(
                context,
                request.StableEntityId,
                std::move(beforeRecipe),
                std::move(afterRecipe),
                std::move(beforeState),
                std::move(afterState));
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
                AdaptPayloadForMaterialSlot(request, bake.Payload),
                build.GeneratedAssetPath,
                request.ExistingGeneratedTexture);
            if (loaded.Status != SelectedMeshTextureBakeStatus::Success)
            {
                return FailureResult(
                    loaded.Status,
                    "failed to load or reload generated texture payload");
            }

            std::uint64_t recipeGeneration = 0u;
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
                          recipeGeneration)
                    : SetReadyBindingDirect(
                          *context.Scene,
                          request,
                          loaded.Asset,
                          build,
                          "mesh texture bake ready",
                          recipeGeneration);
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
                .RecipeGeneration = recipeGeneration,
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

        // The bake payload itself travels to the main thread in the shared
        // `bakeState` slot the worker fills, not in the envelope — it is large
        // and the apply body already owns it. The envelope carries the retired
        // `DerivedJobOutput`'s two observable fields so the completion is
        // non-empty (an empty envelope is how `JobService` reports a dropped
        // job) and stays typed to this lane.
        struct SelectedMeshBakeJobResult
        {
            std::uint64_t PayloadByteCount{0u};
            std::string Diagnostic{};
        };

        // The retired registry's five-way staleness reason collapses onto
        // `JobApplyValidation`: a vanished entity or binding component is
        // `MissingTarget`, a bumped binding generation is `StaleGeneration`.
        // Either way the result is discarded before it can mutate anything.
        [[nodiscard]] JobApplyValidation ValidateAsyncApply(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const std::uint64_t expectedRecipeGeneration)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            const ECS::EntityHandle entity =
                ResolveEntity(*context.Scene, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return JobApplyValidation::MissingTarget;

            if (request.BindGeneratedTexture)
            {
                const auto* state =
                    context.Scene->Raw()
                        .try_get<GeometryPresentationRuntimeState>(entity);
                if (state == nullptr)
                    return JobApplyValidation::MissingTarget;
                if (state->RecipeGeneration != expectedRecipeGeneration)
                    return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }
    }

    BakedPropertyTextureRepresentation
    ResolveBakedPropertyTextureRepresentation(
        const Geometry::PropertyValueKind valueKind,
        const SelectedMeshTextureBakeStorage requestedStorage,
        const MeshAttributeTextureBakeEncoder requestedEncoder,
        const std::span<const BakedPropertyTextureConsumer> consumers) noexcept
    {
        BakedPropertyTextureRepresentation representation{
            .Storage = requestedStorage,
            .Encoder = requestedEncoder,
        };

        const bool hasNormalConsumer = std::ranges::any_of(
            consumers,
            [](const BakedPropertyTextureConsumer& consumer)
            {
                return consumer.Semantic == GeometryPresentationSlotSemantic::Normal;
            });
        const bool hasLinearPbrConsumer = std::ranges::any_of(
            consumers,
            [](const BakedPropertyTextureConsumer& consumer)
            {
                return consumer.Semantic ==
                           GeometryPresentationSlotSemantic::Roughness ||
                       consumer.Semantic ==
                           GeometryPresentationSlotSemantic::Metallic;
            });

        if (representation.Storage == SelectedMeshTextureBakeStorage::Auto)
        {
            representation.Storage =
                hasNormalConsumer ||
                    valueKind == Geometry::PropertyValueKind::UInt32
                ? SelectedMeshTextureBakeStorage::EncodedRgba
                : SelectedMeshTextureBakeStorage::RawFloat;
        }

        if (representation.Encoder != MeshAttributeTextureBakeEncoder::Auto)
            return representation;

        if (hasNormalConsumer)
        {
            representation.Encoder = MeshAttributeTextureBakeEncoder::Normal;
        }
        else if (valueKind == Geometry::PropertyValueKind::UInt32)
        {
            representation.Encoder =
                MeshAttributeTextureBakeEncoder::LabelPalette;
        }
        else if (IsScalarKind(valueKind))
        {
            representation.Encoder = hasLinearPbrConsumer
                ? MeshAttributeTextureBakeEncoder::LinearScalar
                : representation.Storage ==
                      SelectedMeshTextureBakeStorage::EncodedRgba
                    ? MeshAttributeTextureBakeEncoder::ScalarColormap
                    : MeshAttributeTextureBakeEncoder::LinearScalar;
        }
        else if (valueKind == Geometry::PropertyValueKind::Vec2)
        {
            representation.Encoder = MeshAttributeTextureBakeEncoder::Vector2;
        }
        else if (valueKind == Geometry::PropertyValueKind::Vec3)
        {
            representation.Encoder = MeshAttributeTextureBakeEncoder::Vector3;
        }
        else if (valueKind == Geometry::PropertyValueKind::Vec4)
        {
            representation.Encoder = MeshAttributeTextureBakeEncoder::RgbaColor;
        }
        return representation;
    }

    bool IsBakedPropertyTextureConsumerCompatible(
        const BakedPropertyTextureConsumer& consumer,
        const Geometry::PropertyValueKind valueKind,
        const SelectedMeshTextureBakeStorage storage,
        const MeshAttributeTextureBakeEncoder encoder) noexcept
    {
        if (!IsBakedPropertyTextureRepresentationCompatible(
                valueKind,
                storage,
                encoder))
        {
            return false;
        }
        const bool raw =
            storage == SelectedMeshTextureBakeStorage::RawFloat;
        const bool scalar = IsScalarKind(valueKind);
        switch (consumer.Semantic)
        {
        case GeometryPresentationSlotSemantic::Normal:
            return valueKind == Geometry::PropertyValueKind::Vec3 &&
                   storage == SelectedMeshTextureBakeStorage::EncodedRgba &&
                   encoder == MeshAttributeTextureBakeEncoder::Normal;
        case GeometryPresentationSlotSemantic::Roughness:
        case GeometryPresentationSlotSemantic::Metallic:
            return scalar &&
                   (raw ||
                    encoder == MeshAttributeTextureBakeEncoder::LinearScalar);
        case GeometryPresentationSlotSemantic::ScalarField:
            return (scalar &&
                    (raw ||
                     encoder == MeshAttributeTextureBakeEncoder::LinearScalar ||
                     encoder == MeshAttributeTextureBakeEncoder::ScalarColormap)) ||
                   (valueKind == Geometry::PropertyValueKind::UInt32 &&
                    !raw &&
                    encoder == MeshAttributeTextureBakeEncoder::LabelPalette);
        case GeometryPresentationSlotSemantic::Albedo:
            if (scalar)
            {
                return raw ||
                       encoder == MeshAttributeTextureBakeEncoder::LinearScalar ||
                       encoder == MeshAttributeTextureBakeEncoder::ScalarColormap;
            }
            if (valueKind == Geometry::PropertyValueKind::UInt32)
            {
                return !raw &&
                       encoder == MeshAttributeTextureBakeEncoder::LabelPalette;
            }
            if (valueKind == Geometry::PropertyValueKind::Vec2)
            {
                return raw ||
                       encoder == MeshAttributeTextureBakeEncoder::Vector2 ||
                       encoder == MeshAttributeTextureBakeEncoder::RgbaColor;
            }
            if (valueKind == Geometry::PropertyValueKind::Vec3)
            {
                return raw ||
                       encoder == MeshAttributeTextureBakeEncoder::Vector3 ||
                       encoder == MeshAttributeTextureBakeEncoder::RgbaColor ||
                       encoder == MeshAttributeTextureBakeEncoder::Normal;
            }
            return valueKind == Geometry::PropertyValueKind::Vec4 &&
                   (raw ||
                    encoder == MeshAttributeTextureBakeEncoder::RgbaColor);
        case GeometryPresentationSlotSemantic::Displacement:
        case GeometryPresentationSlotSemantic::PointColor:
        case GeometryPresentationSlotSemantic::PointScalarField:
        case GeometryPresentationSlotSemantic::PointSize:
        case GeometryPresentationSlotSemantic::PointNormalOrientation:
        case GeometryPresentationSlotSemantic::LineColor:
        case GeometryPresentationSlotSemantic::LineScalarField:
        case GeometryPresentationSlotSemantic::LineWidth:
            return false;
        }
        return false;
    }

    bool IsBakedPropertyTextureRepresentationCompatible(
        const Geometry::PropertyValueKind valueKind,
        const SelectedMeshTextureBakeStorage storage,
        const MeshAttributeTextureBakeEncoder encoder) noexcept
    {
        if (storage == SelectedMeshTextureBakeStorage::Auto ||
            encoder == MeshAttributeTextureBakeEncoder::Auto)
        {
            return false;
        }

        if (storage == SelectedMeshTextureBakeStorage::RawFloat)
        {
            switch (valueKind)
            {
            case Geometry::PropertyValueKind::Float:
            case Geometry::PropertyValueKind::Double:
                return encoder ==
                    MeshAttributeTextureBakeEncoder::LinearScalar;
            case Geometry::PropertyValueKind::Vec2:
                return encoder == MeshAttributeTextureBakeEncoder::Vector2;
            case Geometry::PropertyValueKind::Vec3:
                return encoder == MeshAttributeTextureBakeEncoder::Vector3;
            case Geometry::PropertyValueKind::Vec4:
                return encoder == MeshAttributeTextureBakeEncoder::RgbaColor;
            case Geometry::PropertyValueKind::UInt32:
            case Geometry::PropertyValueKind::Unknown:
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
                return false;
            }
            return false;
        }

        if (storage != SelectedMeshTextureBakeStorage::EncodedRgba)
            return false;

        switch (valueKind)
        {
        case Geometry::PropertyValueKind::Float:
        case Geometry::PropertyValueKind::Double:
            return encoder ==
                       MeshAttributeTextureBakeEncoder::LinearScalar ||
                   encoder ==
                       MeshAttributeTextureBakeEncoder::ScalarColormap;
        case Geometry::PropertyValueKind::UInt32:
            return encoder ==
                MeshAttributeTextureBakeEncoder::LabelPalette;
        case Geometry::PropertyValueKind::Vec2:
            return encoder == MeshAttributeTextureBakeEncoder::Vector2 ||
                   encoder == MeshAttributeTextureBakeEncoder::RgbaColor;
        case Geometry::PropertyValueKind::Vec3:
            return encoder == MeshAttributeTextureBakeEncoder::Vector3 ||
                   encoder == MeshAttributeTextureBakeEncoder::RgbaColor ||
                   encoder == MeshAttributeTextureBakeEncoder::Normal;
        case Geometry::PropertyValueKind::Vec4:
            return encoder == MeshAttributeTextureBakeEncoder::RgbaColor;
        case Geometry::PropertyValueKind::Unknown:
        case Geometry::PropertyValueKind::Bool:
        case Geometry::PropertyValueKind::Int32:
        case Geometry::PropertyValueKind::UInt64:
            return false;
        }
        return false;
    }

    const char* DebugNameForSelectedMeshTextureBakeStatus(
        const SelectedMeshTextureBakeStatus status) noexcept
    {
        switch (status)
        {
        case SelectedMeshTextureBakeStatus::Success: return "SelectedMeshTextureBake.Success";
        case SelectedMeshTextureBakeStatus::Scheduled: return "SelectedMeshTextureBake.Scheduled";
        case SelectedMeshTextureBakeStatus::NonOperationalBackend: return "SelectedMeshTextureBake.NonOperationalBackend";
        case SelectedMeshTextureBakeStatus::MissingScene: return "SelectedMeshTextureBake.MissingScene";
        case SelectedMeshTextureBakeStatus::MissingAssetService: return "SelectedMeshTextureBake.MissingAssetService";
        case SelectedMeshTextureBakeStatus::StaleEntity: return "SelectedMeshTextureBake.StaleEntity";
        case SelectedMeshTextureBakeStatus::NonMeshSelection: return "SelectedMeshTextureBake.NonMeshSelection";
        case SelectedMeshTextureBakeStatus::MissingGeometryPresentationRecipe: return "SelectedMeshTextureBake.MissingGeometryPresentationRecipe";
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
            request.TargetLane != GeometryRenderLane::Surface)
        {
            return FailureBuild(
                SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic);
        }

        const GS::ConstSourceView view = GS::BuildConstView(scene.Raw(), entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::Mesh ||
            !availability.Has(GS::SourceCapability::VertexPoints) ||
            !availability.Has(GS::SourceCapability::Halfedges) ||
            !availability.Has(GS::SourceCapability::Faces))
        {
            return FailureBuild(SelectedMeshTextureBakeStatus::NonMeshSelection);
        }

        SelectedMeshTextureBakeStatus domainStatus =
            SelectedMeshTextureBakeStatus::Success;
        const MeshAttributeTextureBakeSourceDomain bakeDomain =
            ToBakeDomain(request.SourceDomain, domainStatus);
        if (domainStatus != SelectedMeshTextureBakeStatus::Success)
            return FailureBuild(domainStatus);

        const GeometryEntityAvailability geometryAvailability =
            BuildGeometryAvailability(view);
        const std::size_t expectedCount =
            ResolveGeometryElementCount(
                geometryAvailability, request.SourceDomain);
        GeometryPropertyValueKindFilter expectedKind = request.ExpectedValueKind;
        if (!expectedKind.has_value())
            expectedKind = DefaultExpectedKindForSemantic(request.TargetSemantic);

        GeometryPropertyRef descriptor{
            .Domain = request.SourceDomain,
            .Name = request.SourcePropertyName,
            .ValueKind = expectedKind.value_or(
                Geometry::PropertyValueKind::Unknown),
        };
        GeometryPropertyResolution resolution =
            ResolveGeometryProperty(
                geometryAvailability,
                descriptor,
                expectedCount,
                true,
                request.SourceGeneration);
        if (!resolution.Resolved())
        {
            return FailureBuild(
                StatusForResolution(resolution.Status),
                std::string{ToString(resolution.Status)});
        }
        const MeshAttributeTextureBakeEncoder resolvedEncoder =
            ResolveMaterialSlotEncoder(request, resolution.ResolvedValueKind);
        if (!EncoderCanHandle(resolvedEncoder, resolution.ResolvedValueKind))
        {
            return FailureBuild(
                SelectedMeshTextureBakeStatus::IncompatibleTargetSlot,
                "encoder is incompatible with the selected property type");
        }

        std::string compatibilityDiagnostic{};
        const SelectedMeshTextureBakeStatus compatibility =
            ValidateTargetSlotCompatibility(
                request.TargetSemantic,
                resolution.ResolvedValueKind,
                resolvedEncoder,
                compatibilityDiagnostic);
        if (compatibility != SelectedMeshTextureBakeStatus::Success)
        {
            return FailureBuild(
                compatibility,
                std::move(compatibilityDiagnostic));
        }

        if (request.BindGeneratedTexture)
        {
            const auto* bindings =
                scene.Raw().try_get<GeometryPresentationRecipe>(entity);
            if (bindings == nullptr)
                return FailureBuild(
                    SelectedMeshTextureBakeStatus::MissingGeometryPresentationRecipe);

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
            ? ToBakeValueKind(resolution.ResolvedValueKind)
            : request.ValueKind;
        bake.TargetSemantic = std::string{ToString(request.TargetSemantic)};
        bake.Encoder = resolvedEncoder;
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

        if (ShouldUseObjectSpaceNormalBakeQueue(context, request))
        {
            return ScheduleSelectedMeshObjectSpaceNormalBake(
                context,
                request,
                build,
                view);
        }

        if (context.AssetService == nullptr)
            return FailureResult(SelectedMeshTextureBakeStatus::MissingAssetService);

        const bool useAsyncJob =
            request.PreferAsyncJob && context.Jobs != nullptr;
        if (!useAsyncJob)
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

        std::uint64_t recipeGeneration = 0u;
        bool previousOutputRetained = false;
        const SelectedMeshTextureBakeStatus pendingStatus =
            SetPendingBinding(
                context,
                request,
                build,
                recipeGeneration,
                previousOutputRetained);
        if (pendingStatus != SelectedMeshTextureBakeStatus::Success)
            return FailureResult(pendingStatus);

        MeshBakeSourceSnapshot snapshot = SnapshotMeshSources(view);
        auto bakeState = std::make_shared<std::optional<MeshAttributeTextureBakeResult>>();

        SelectedMeshTextureBakeContext applyContext = context;
        SelectedMeshTextureBakeRequest applyRequest = request;
        SelectedMeshTextureBakeBuildResult applyBuild = build;
        const std::uint64_t expectedRecipeGeneration = recipeGeneration;
        JobDesc desc{};
        desc.DebugName = "selected mesh texture bake";
        desc.Kind = RuntimeTaskKinds::GeometryProcess;
        desc.EstimatedCost = std::max<std::uint32_t>(
            1u,
            request.Width * request.Height / 1024u);
        desc.Scope = context.World;
        desc.Work =
            [snapshot = std::move(snapshot),
             bakeRequest = build.BakeRequest,
             bakeState](const JobCancellation&) mutable -> JobResultEnvelope
            {
                *bakeState = BakeMeshAttributeTexture(
                    snapshot.View(),
                    bakeRequest);
                const MeshAttributeTextureBakeResult& bake = **bakeState;
                if (bake.Status != MeshAttributeTextureBakeStatus::Success)
                {
                    return JobResultEnvelope::Make<SelectedMeshBakeJobResult>(
                        SelectedMeshBakeJobResult{
                            .Diagnostic = BuildBakeDiagnostic(bake.Status),
                        });
                }
                return JobResultEnvelope::Make<SelectedMeshBakeJobResult>(
                    SelectedMeshBakeJobResult{
                        .PayloadByteCount =
                            static_cast<std::uint64_t>(bake.Payload.PixelBytes.size()),
                        .Diagnostic = "mesh texture bake ready",
                    });
            };
        desc.ValidateBeforeApply =
            [applyContext, applyRequest, expectedRecipeGeneration]()
            {
                return ValidateAsyncApply(
                    applyContext,
                    applyRequest,
                    expectedRecipeGeneration);
            };
        desc.PublishCompletion =
            [applyContext,
             applyRequest = std::move(applyRequest),
             applyBuild = std::move(applyBuild),
             bakeState](KernelEventBus&,
                        const JobResultEnvelope& result) mutable -> bool
            {
                if (result.TryGet<SelectedMeshBakeJobResult>() == nullptr ||
                    !bakeState->has_value())
                {
                    return false;
                }
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
                            auto& raw = applyContext.Scene->Raw();
                            auto* recipe =
                                raw.try_get<GeometryPresentationRecipe>(
                                    entity);
                            auto* runtimeState =
                                raw.try_get<GeometryPresentationRuntimeState>(
                                    entity);
                            if (recipe != nullptr && runtimeState != nullptr)
                            {
                                if (SlotLookup lookup =
                                        FindMutableSlot(*recipe, applyRequest);
                                    lookup.Presentation != nullptr &&
                                    lookup.Slot != nullptr)
                                {
                                    GeometryPresentationSlotStatus& status =
                                        EnsurePresentationSlotStatus(
                                            *runtimeState,
                                            lookup.Presentation->Key,
                                            applyRequest.TargetSemantic);
                                    status.Readiness =
                                        GeometryPresentationReadiness::Failed;
                                    status.Diagnostic =
                                        BuildBakeDiagnostic(bake.Status);
                                }
                            }
                        }
                    }
                    return false;
                }

                SelectedMeshTextureBakeResult applied =
                    ApplyBakePayload(
                        applyContext,
                        applyRequest,
                        applyBuild,
                        bake,
                        false);
                return applied.Succeeded();
            };

        const JobToken handle = context.Jobs->Submit(std::move(desc));
        if (!handle.IsValid())
            return FailureResult(SelectedMeshTextureBakeStatus::JobSubmitFailed);

        return SelectedMeshTextureBakeResult{
            .Status = SelectedMeshTextureBakeStatus::Scheduled,
            .GeneratedTexture = request.ExistingGeneratedTexture,
            .Job = handle,
            .ExecutionMode = SelectedMeshTextureBakeExecutionMode::AsyncJob,
            .BoundGeneratedTexture = false,
            .PreviousOutputRetained = previousOutputRetained,
            .RecipeGeneration = recipeGeneration,
            .GeneratedAssetPath = build.GeneratedAssetPath,
            .Diagnostic = "mesh texture bake scheduled",
        };
    }
}
