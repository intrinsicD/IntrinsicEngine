module;

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
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
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.SelectionController;
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

        [[nodiscard]] bool IsScalarKind(const ProgressivePropertyValueKind kind) noexcept
        {
            return kind == ProgressivePropertyValueKind::ScalarFloat ||
                   kind == ProgressivePropertyValueKind::ScalarDouble;
        }

        [[nodiscard]] MeshAttributeTextureBakeEncoder ResolveMaterialSlotEncoder(
            const SelectedMeshTextureBakeRequest& request,
            const ProgressivePropertyValueKind kind) noexcept
        {
            if (request.Encoder != MeshAttributeTextureBakeEncoder::Auto)
                return request.Encoder;

            switch (request.TargetSemantic)
            {
            case ProgressiveSlotSemantic::Normal:
                return MeshAttributeTextureBakeEncoder::Normal;
            case ProgressiveSlotSemantic::Roughness:
            case ProgressiveSlotSemantic::Metallic:
            case ProgressiveSlotSemantic::Displacement:
                return MeshAttributeTextureBakeEncoder::LinearScalar;
            case ProgressiveSlotSemantic::ScalarField:
                return kind == ProgressivePropertyValueKind::UInt32
                    ? MeshAttributeTextureBakeEncoder::LabelPalette
                    : MeshAttributeTextureBakeEncoder::ScalarColormap;
            case ProgressiveSlotSemantic::Albedo:
                switch (kind)
                {
                case ProgressivePropertyValueKind::ScalarFloat:
                case ProgressivePropertyValueKind::ScalarDouble:
                    return MeshAttributeTextureBakeEncoder::ScalarColormap;
                case ProgressivePropertyValueKind::UInt32:
                    return MeshAttributeTextureBakeEncoder::LabelPalette;
                case ProgressivePropertyValueKind::Vec3:
                case ProgressivePropertyValueKind::Vec4:
                    return MeshAttributeTextureBakeEncoder::RgbaColor;
                case ProgressivePropertyValueKind::Vec2:
                    return MeshAttributeTextureBakeEncoder::Vector2;
                case ProgressivePropertyValueKind::Any:
                case ProgressivePropertyValueKind::Unknown:
                    break;
                }
                break;
            case ProgressiveSlotSemantic::PointColor:
            case ProgressiveSlotSemantic::PointScalarField:
            case ProgressiveSlotSemantic::PointSize:
            case ProgressiveSlotSemantic::PointNormalOrientation:
            case ProgressiveSlotSemantic::LineColor:
            case ProgressiveSlotSemantic::LineScalarField:
            case ProgressiveSlotSemantic::LineWidth:
                break;
            }

            return MeshAttributeTextureBakeEncoder::Auto;
        }

        [[nodiscard]] SelectedMeshTextureBakeStatus ValidateTargetSlotCompatibility(
            const ProgressiveSlotSemantic semantic,
            const ProgressivePropertyValueKind kind,
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
            case ProgressiveSlotSemantic::Normal:
                if (kind == ProgressivePropertyValueKind::Vec3 &&
                    encoder == MeshAttributeTextureBakeEncoder::Normal)
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("normal material slot requires a vec3 property encoded as a normal texture");
            case ProgressiveSlotSemantic::Roughness:
            case ProgressiveSlotSemantic::Metallic:
                if (IsScalarKind(kind) &&
                    encoder == MeshAttributeTextureBakeEncoder::LinearScalar)
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("roughness and metallic material slots require scalar properties encoded as linear scalar textures");
            case ProgressiveSlotSemantic::Albedo:
                if ((IsScalarKind(kind) &&
                     (encoder == MeshAttributeTextureBakeEncoder::LinearScalar ||
                      encoder == MeshAttributeTextureBakeEncoder::ScalarColormap)) ||
                    (kind == ProgressivePropertyValueKind::UInt32 &&
                     encoder == MeshAttributeTextureBakeEncoder::LabelPalette) ||
                    ((kind == ProgressivePropertyValueKind::Vec3 ||
                      kind == ProgressivePropertyValueKind::Vec4) &&
                     encoder == MeshAttributeTextureBakeEncoder::RgbaColor))
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("albedo material slot requires a color, scalar colormap, scalar grayscale, or label-palette texture");
            case ProgressiveSlotSemantic::ScalarField:
                if ((IsScalarKind(kind) &&
                     (encoder == MeshAttributeTextureBakeEncoder::LinearScalar ||
                      encoder == MeshAttributeTextureBakeEncoder::ScalarColormap)) ||
                    (kind == ProgressivePropertyValueKind::UInt32 &&
                     encoder == MeshAttributeTextureBakeEncoder::LabelPalette))
                {
                    return SelectedMeshTextureBakeStatus::Success;
                }
                return incompatible("scalar-field material slot requires scalar or label properties");
            case ProgressiveSlotSemantic::Displacement:
                diagnostic = "displacement bake output has no backend-resident material texture slot yet";
                return SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic;
            case ProgressiveSlotSemantic::PointColor:
            case ProgressiveSlotSemantic::PointScalarField:
            case ProgressiveSlotSemantic::PointSize:
            case ProgressiveSlotSemantic::PointNormalOrientation:
            case ProgressiveSlotSemantic::LineColor:
            case ProgressiveSlotSemantic::LineScalarField:
            case ProgressiveSlotSemantic::LineWidth:
                diagnostic = "target semantic is not a mesh surface material texture slot";
                return SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic;
            }

            return SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic;
        }

        [[nodiscard]] bool IsPackedMetallicRoughnessTarget(
            const ProgressiveSlotSemantic semantic) noexcept
        {
            return semantic == ProgressiveSlotSemantic::Roughness ||
                   semantic == ProgressiveSlotSemantic::Metallic;
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
                packed[offset + 1u] = request.TargetSemantic == ProgressiveSlotSemantic::Roughness
                    ? scalar
                    : std::byte{0xFF};
                packed[offset + 2u] = request.TargetSemantic == ProgressiveSlotSemantic::Metallic
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

        [[nodiscard]] std::uint32_t NarrowBakeCount(
            const std::size_t value) noexcept
        {
            return value > std::numeric_limits<std::uint32_t>::max()
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] std::uint64_t MixObjectSpaceNormalBakeKey(
            std::uint64_t seed,
            const std::uint64_t value) noexcept
        {
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
            return seed == 0u ? 1u : seed;
        }

        [[nodiscard]] std::uint64_t FloatKeyBits(const float value) noexcept
        {
            return static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value));
        }

        [[nodiscard]] std::uint64_t HashVec2Property(
            const Geometry::ConstProperty<glm::vec2>& property) noexcept
        {
            if (!property.IsValid())
                return 0u;

            std::uint64_t hash = 0xcbf29ce484222325ull;
            hash = MixObjectSpaceNormalBakeKey(hash, property.Vector().size());
            for (const glm::vec2& value : property.Vector())
            {
                hash = MixObjectSpaceNormalBakeKey(hash, FloatKeyBits(value.x));
                hash = MixObjectSpaceNormalBakeKey(hash, FloatKeyBits(value.y));
            }
            return hash == 0u ? 1u : hash;
        }

        [[nodiscard]] std::uint64_t HashVec3Property(
            const Geometry::ConstProperty<glm::vec3>& property) noexcept
        {
            if (!property.IsValid())
                return 0u;

            std::uint64_t hash = 0xcbf29ce484222325ull;
            hash = MixObjectSpaceNormalBakeKey(hash, property.Vector().size());
            for (const glm::vec3& value : property.Vector())
            {
                hash = MixObjectSpaceNormalBakeKey(hash, FloatKeyBits(value.x));
                hash = MixObjectSpaceNormalBakeKey(hash, FloatKeyBits(value.y));
                hash = MixObjectSpaceNormalBakeKey(hash, FloatKeyBits(value.z));
            }
            return hash == 0u ? 1u : hash;
        }

        [[nodiscard]] Geometry::ConstProperty<glm::vec3>
        ResolveBakePositionProperty(
            const Geometry::ConstPropertySet& vertexProperties)
        {
            auto positions = vertexProperties.Get<glm::vec3>("v:point");
            if (!positions.IsValid())
            {
                positions = vertexProperties.Get<glm::vec3>(
                    ECS::Components::GeometrySources::PropertyNames::kPosition);
            }
            return positions;
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeContentKey
        BuildSelectedMeshObjectSpaceNormalBakeContentKey(
            const Geometry::ConstPropertySet& vertexProperties,
            const SelectedMeshTextureBakeRequest& request,
            const std::uint32_t vertexCount,
            const std::uint32_t indexCount)
        {
            const auto positions = ResolveBakePositionProperty(vertexProperties);
            const auto texcoords =
                vertexProperties.Get<glm::vec2>(request.TexcoordPropertyName);
            const auto normals =
                vertexProperties.Get<glm::vec3>(request.SourcePropertyName);

            std::uint64_t geometryKey = 0x84222325cbf29ce4ull;
            geometryKey = MixObjectSpaceNormalBakeKey(geometryKey, vertexCount);
            geometryKey = MixObjectSpaceNormalBakeKey(geometryKey, indexCount);
            const std::uint64_t positionKey = HashVec3Property(positions);
            geometryKey = MixObjectSpaceNormalBakeKey(
                geometryKey,
                positionKey != 0u ? positionKey : vertexCount);

            return RuntimeObjectSpaceNormalBakeContentKey{
                .GeometryKey = geometryKey,
                .TexcoordKey = HashVec2Property(texcoords),
                .NormalKey = HashVec3Property(normals),
                .VertexCount = vertexCount,
                .IndexCount = indexCount,
            };
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
            const SelectedMeshTextureBakeRequest& request)
        {
            if (request.StableEntityId == 0u ||
                view.VertexSource == nullptr)
            {
                return FailureObjectSpaceNormalBakeRequestBuild(
                    SelectedMeshTextureBakeStatus::StaleEntity);
            }

            const Geometry::ConstPropertySet vertexProperties{
                view.VertexSource->Properties};
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

            const std::uint32_t vertexCount =
                NarrowBakeCount(view.VerticesAlive());
            const std::uint32_t indexCount =
                NarrowBakeCount(view.FacesAlive() * 3u);
            const RuntimeObjectSpaceNormalBakeContentKey contentKey =
                BuildSelectedMeshObjectSpaceNormalBakeContentKey(
                    vertexProperties,
                    request,
                    vertexCount,
                    indexCount);

            if (!contentKey.IsValid())
            {
                return FailureObjectSpaceNormalBakeRequestBuild(
                    SelectedMeshTextureBakeStatus::BakeFailed,
                    "selected mesh object-space normal bake has no stable geometry/uv/normal content key");
            }

            Graphics::ObjectSpaceNormalTextureBakeOptions bakeOptions{};
            bakeOptions.Width = request.Width;
            bakeOptions.Height = request.Height;
            bakeOptions.Space = Graphics::NormalTextureSpace::ObjectSpaceNormal;

            const std::uint64_t geometryGeneration =
                request.DirtyStamp != 0u ? request.DirtyStamp : 1u;
            const std::uint64_t propertyGeneration =
                request.SourceGeneration != 0u
                    ? request.SourceGeneration
                    : geometryGeneration;

            return SelectedObjectSpaceNormalBakeRequestBuild{
                .Request = RuntimeObjectSpaceNormalBakeRequest{
                    .EntityScopedGeneratedTextureAsset =
                        Assets::AssetId{request.StableEntityId, 1u},
                    .SourceKey =
                        Graphics::ObjectSpaceNormalTextureBakeSourceKey{
                            .EntityKey = request.StableEntityId,
                            .GeometryGeneration = geometryGeneration,
                            .TexcoordGeneration = propertyGeneration,
                            .NormalGeneration = propertyGeneration,
                        },
                    .EntityGeneration = request.StableEntityId,
                    .Options = bakeOptions,
                    .ContentKey = contentKey,
                    .HasStableContentKey = true,
                },
            };
        }

        [[nodiscard]] bool ShouldUseObjectSpaceNormalBakeQueue(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request) noexcept
        {
            return context.ObjectSpaceNormalBakeQueue != nullptr &&
                   request.BindGeneratedTexture &&
                   request.TargetLane == ProgressiveRenderLane::Surface &&
                   request.TargetSemantic == ProgressiveSlotSemantic::Normal &&
                   request.SourceDomain == ProgressiveGeometryDomain::MeshVertex;
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
            case RuntimeObjectSpaceNormalBakeStatus::MissingGeneratedTextureAsset:
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

        [[nodiscard]] SelectedMeshTextureBakeResult
        ScheduleSelectedMeshObjectSpaceNormalBake(
            const SelectedMeshTextureBakeContext& context,
            const SelectedMeshTextureBakeRequest& request,
            const SelectedMeshTextureBakeBuildResult& build,
            const GS::ConstSourceView& view)
        {
            if (context.ObjectSpaceNormalBakeQueue == nullptr)
                return FailureResult(SelectedMeshTextureBakeStatus::CommandFailed);

            SelectedObjectSpaceNormalBakeRequestBuild queueBuild =
                BuildSelectedMeshObjectSpaceNormalBakeRequest(view, request);
            if (!queueBuild.Succeeded())
            {
                SelectedMeshTextureBakeResult result =
                    FailureResult(queueBuild.Status, queueBuild.Diagnostic);
                result.GeneratedAssetPath = build.GeneratedAssetPath;
                return result;
            }

            if (!context.ObjectSpaceNormalBakeGraphicsBackendOperational)
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
                result.BindingGeneration = bindingGeneration;
                result.PreviousOutputRetained = previousOutputRetained;
                return result;
            }

            return SelectedMeshTextureBakeResult{
                .Status = SelectedMeshTextureBakeStatus::Scheduled,
                .GeneratedTexture = queued.Submission.GeneratedTextureAsset,
                .ExecutionMode =
                    SelectedMeshTextureBakeExecutionMode::ObjectSpaceNormalBakeQueue,
                .BoundGeneratedTexture = request.BindGeneratedTexture,
                .PreviousOutputRetained = previousOutputRetained,
                .BindingGeneration = bindingGeneration,
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
                AdaptPayloadForMaterialSlot(request, bake.Payload),
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
        case SelectedMeshTextureBakeStatus::NonOperationalBackend: return "SelectedMeshTextureBake.NonOperationalBackend";
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
        const MeshAttributeTextureBakeEncoder resolvedEncoder =
            ResolveMaterialSlotEncoder(request, resolution.ActualValueKind);
        if (!EncoderCanHandle(resolvedEncoder, resolution.ActualValueKind))
        {
            return FailureBuild(
                SelectedMeshTextureBakeStatus::IncompatibleTargetSlot,
                "encoder is incompatible with the selected property type");
        }

        std::string compatibilityDiagnostic{};
        const SelectedMeshTextureBakeStatus compatibility =
            ValidateTargetSlotCompatibility(
                request.TargetSemantic,
                resolution.ActualValueKind,
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
