module;

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.AssetModelSceneHandoff;

import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Hierarchy.Structure;
import Extrinsic.ECS.System.BoundsPropagation;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetMeshNormals;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.HalfedgeMesh.IO;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        struct PreparedPrimitive
        {
            std::uint32_t PrimitiveIndex{Assets::kInvalidAssetModelIndex};
            std::uint32_t GeometryPayloadIndex{Assets::kInvalidAssetModelIndex};
            std::uint32_t MaterialIndex{Assets::kInvalidAssetModelIndex};
            std::uint32_t VertexCount{0u};
            std::uint32_t IndexCount{0u};
            std::string Name{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            ECS::Components::Culling::Local::Bounds LocalBounds{};
            bool HasResolvedTexcoords{false};
            RuntimeMeshResolvedUvProvenance TexcoordProvenance{
                RuntimeMeshResolvedUvProvenance::None};
        };

        struct PreparedNode
        {
            ECS::Components::Transform::Component LocalTransform{};
            glm::mat4 WorldMatrix{1.0f};
        };

        struct PreparedPrimitiveInstance
        {
            std::uint32_t NodeIndex{Assets::kInvalidAssetModelIndex};
            std::uint32_t PrimitiveIndex{Assets::kInvalidAssetModelIndex};
            ECS::Components::Culling::World::Bounds WorldBounds{};
        };

        struct GeneratedMaterialTextureAssets
        {
            Assets::AssetId Albedo{};
            Assets::AssetId Normal{};
        };

        struct RuntimeModelSceneRecord
        {
            AssetModelSceneHandoffState State{};
        };

        [[nodiscard]] bool IsTypeMismatch(const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::TypeMismatch
                || error == Core::ErrorCode::AssetTypeMismatch;
        }

        [[nodiscard]] bool IsUploadDeferral(const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::DeviceNotOperational
                || error == Core::ErrorCode::ResourceBusy;
        }

        [[nodiscard]] const char* ExtensionFor(
            const Assets::AssetFileFormat format) noexcept
        {
            switch (format)
            {
            case Assets::AssetFileFormat::PNG:
                return ".png";
            case Assets::AssetFileFormat::JPEG:
                return ".jpg";
            case Assets::AssetFileFormat::TGA:
                return ".tga";
            case Assets::AssetFileFormat::BMP:
                return ".bmp";
            case Assets::AssetFileFormat::HDR:
                return ".hdr";
            case Assets::AssetFileFormat::KTX:
                return ".ktx";
            default:
                return ".texture";
            }
        }

        [[nodiscard]] bool IsStablePathCharacter(const char c) noexcept
        {
            return (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') ||
                   c == '-' ||
                   c == '_';
        }

        [[nodiscard]] std::string SanitizePathToken(const std::string_view token)
        {
            std::string sanitized{};
            sanitized.reserve(token.size());
            for (const char c : token)
            {
                sanitized.push_back(IsStablePathCharacter(c) ? c : '-');
            }
            if (sanitized.empty())
            {
                return "property";
            }
            return sanitized;
        }

        void RecordFailure(
            AssetModelSceneHandoffDiagnostics* diagnostics,
            const Assets::AssetId modelAsset,
            const Core::ErrorCode error)
        {
            if (diagnostics == nullptr)
            {
                return;
            }
            diagnostics->LastFailedAsset = modelAsset;
            diagnostics->LastError = error;
            ++diagnostics->ModelSceneMaterializeFailures;
        }

        void RecordUvMaterializationDiagnostics(
            AssetModelSceneHandoffDiagnostics* diagnostics,
            const RuntimeMeshMaterializationDiagnostics& uvDiagnostics)
        {
            if (diagnostics == nullptr)
            {
                return;
            }

            if (uvDiagnostics.UvAtlasStatus != Geometry::UvAtlas::UvAtlasStatus::Success)
            {
                ++diagnostics->UvAtlasFailures;
            }
            if (uvDiagnostics.AuthoredTexcoordsRejected)
            {
                ++diagnostics->InvalidAuthoredUvPrimitives;
            }
            if (uvDiagnostics.TexcoordProvenance ==
                RuntimeMeshResolvedUvProvenance::AuthoredPreserved)
            {
                ++diagnostics->AuthoredUvPrimitives;
            }
            else if (uvDiagnostics.TexcoordProvenance ==
                     RuntimeMeshResolvedUvProvenance::GeneratedAtlas)
            {
                ++diagnostics->GeneratedUvAtlasPrimitives;
            }
            diagnostics->UvAtlasSeamSplitVertices += uvDiagnostics.SeamSplitVertexCount;
            diagnostics->LastUvAtlasChartCount = uvDiagnostics.ChartCount;
            diagnostics->LastUvAtlasWidth = uvDiagnostics.AtlasWidth;
            diagnostics->LastUvAtlasHeight = uvDiagnostics.AtlasHeight;
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::quat value) noexcept
        {
            return std::isfinite(value.w)
                && std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::mat4& matrix) noexcept
        {
            for (std::size_t column = 0u; column < 4u; ++column)
            {
                for (std::size_t row = 0u; row < 4u; ++row)
                {
                    if (!std::isfinite(matrix[column][row]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] glm::mat4 ToColumnMajorMatrix(
            const std::array<float, 16>& values) noexcept
        {
            glm::mat4 matrix{1.0f};
            for (std::size_t column = 0u; column < 4u; ++column)
            {
                for (std::size_t row = 0u; row < 4u; ++row)
                {
                    matrix[column][row] = values[column * 4u + row];
                }
            }
            return matrix;
        }

        [[nodiscard]] bool MatricesApproximatelyEqual(
            const glm::mat4& lhs,
            const glm::mat4& rhs) noexcept
        {
            constexpr float kAbsoluteTolerance = 1.0e-5f;
            constexpr float kRelativeTolerance = 1.0e-4f;
            for (std::size_t column = 0u; column < 4u; ++column)
            {
                for (std::size_t row = 0u; row < 4u; ++row)
                {
                    const float scale = std::max(
                        {1.0f, std::abs(lhs[column][row]), std::abs(rhs[column][row])});
                    if (std::abs(lhs[column][row] - rhs[column][row])
                        > kAbsoluteTolerance + kRelativeTolerance * scale)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] Core::Expected<ECS::Components::Culling::Local::Bounds>
        ComputeLocalBounds(const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            glm::vec3 minimum{0.0f};
            glm::vec3 maximum{0.0f};
            bool hasVertex = false;
            for (std::size_t index = 0u; index < mesh.VerticesSize(); ++index)
            {
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(index)};
                if (!mesh.IsValid(vertex) || mesh.IsDeleted(vertex))
                {
                    continue;
                }

                const glm::vec3 position = mesh.Position(vertex);
                if (!IsFinite(position))
                {
                    return Core::Err<ECS::Components::Culling::Local::Bounds>(
                        Core::ErrorCode::AssetInvalidData);
                }
                if (!hasVertex)
                {
                    minimum = position;
                    maximum = position;
                    hasVertex = true;
                }
                else
                {
                    minimum = glm::min(minimum, position);
                    maximum = glm::max(maximum, position);
                }
            }
            if (!hasVertex)
            {
                return Core::Err<ECS::Components::Culling::Local::Bounds>(
                    Core::ErrorCode::AssetInvalidData);
            }

            ECS::Components::Culling::Local::Bounds bounds{};
            bounds.LocalBoundingAABB.Min = minimum;
            bounds.LocalBoundingAABB.Max = maximum;
            bounds.LocalBoundingSphere.Center = 0.5f * (minimum + maximum);
            bounds.LocalBoundingSphere.Radius =
                0.5f * glm::length(maximum - minimum);
            if (!std::isfinite(bounds.LocalBoundingSphere.Radius))
            {
                return Core::Err<ECS::Components::Culling::Local::Bounds>(
                    Core::ErrorCode::AssetInvalidData);
            }
            return bounds;
        }

        [[nodiscard]] Core::Expected<ECS::Components::Culling::World::Bounds>
        ComputeWorldBounds(
            const ECS::Components::Culling::Local::Bounds& local,
            const glm::mat4& worldMatrix)
        {
            ECS::Components::Culling::World::Bounds world{};
            if (!ECS::Systems::BoundsPropagation::TryComputeWorldBounds(
                    local,
                    worldMatrix,
                    world))
            {
                return Core::Err<ECS::Components::Culling::World::Bounds>(
                    Core::ErrorCode::AssetInvalidData);
            }

            return world;
        }

        [[nodiscard]] Core::Expected<std::vector<PreparedNode>> PrepareNodes(
            const Assets::AssetModelScenePayload& model)
        {
            std::vector<PreparedNode> prepared(model.Nodes.size());
            std::vector<glm::mat4> localMatrices(model.Nodes.size());
            std::vector<std::uint8_t> state(model.Nodes.size(), 0u);

            for (std::size_t nodeIndex = 0u;
                 nodeIndex < model.Nodes.size();
                 ++nodeIndex)
            {
                const Assets::AssetModelNodePayload& node = model.Nodes[nodeIndex];
                const glm::mat4 localMatrix =
                    ToColumnMajorMatrix(node.LocalTransform);
                constexpr float kAffineTolerance = 1.0e-5f;
                if (!IsFinite(localMatrix)
                    || std::abs(localMatrix[0][3]) > kAffineTolerance
                    || std::abs(localMatrix[1][3]) > kAffineTolerance
                    || std::abs(localMatrix[2][3]) > kAffineTolerance
                    || std::abs(localMatrix[3][3] - 1.0f) > kAffineTolerance)
                {
                    return Core::Err<std::vector<PreparedNode>>(
                        Core::ErrorCode::AssetInvalidData);
                }

                ECS::Components::Transform::Component localTransform{};
                if (!ECS::Components::Transform::TryDecomposeMatrix(
                        localMatrix,
                        localTransform)
                    || !IsFinite(localTransform.Position)
                    || !IsFinite(localTransform.Rotation)
                    || !IsFinite(localTransform.Scale))
                {
                    return Core::Err<std::vector<PreparedNode>>(
                        Core::ErrorCode::AssetInvalidData);
                }

                const glm::mat4 reconstructed =
                    ECS::Components::Transform::GetMatrix(localTransform);
                if (!IsFinite(reconstructed)
                    || !MatricesApproximatelyEqual(localMatrix, reconstructed))
                {
                    return Core::Err<std::vector<PreparedNode>>(
                        Core::ErrorCode::AssetInvalidData);
                }
                prepared[nodeIndex].LocalTransform = localTransform;
                localMatrices[nodeIndex] = reconstructed;
            }

            // Follow parent chains with an explicit work stack. Model payloads
            // are untrusted and can contain arbitrarily deep hierarchies, so a
            // recursive walk would make valid input depth consume call-stack
            // space and turn malformed cycles into a stack-overflow hazard.
            std::vector<std::uint32_t> chain{};
            chain.reserve(model.Nodes.size());
            for (std::size_t startIndex = 0u;
                 startIndex < model.Nodes.size();
                 ++startIndex)
            {
                if (state[startIndex] == 2u)
                {
                    continue;
                }

                chain.clear();
                std::uint32_t nodeIndex = static_cast<std::uint32_t>(startIndex);
                while (nodeIndex != Assets::kInvalidAssetModelIndex)
                {
                    if (nodeIndex >= model.Nodes.size())
                    {
                        return Core::Err<std::vector<PreparedNode>>(
                            Core::ErrorCode::OutOfRange);
                    }
                    if (state[nodeIndex] == 2u)
                    {
                        break;
                    }
                    if (state[nodeIndex] == 1u)
                    {
                        return Core::Err<std::vector<PreparedNode>>(
                            Core::ErrorCode::AssetInvalidData);
                    }

                    state[nodeIndex] = 1u;
                    chain.push_back(nodeIndex);
                    nodeIndex = model.Nodes[nodeIndex].ParentNodeIndex;
                }

                glm::mat4 parentWorld =
                    nodeIndex == Assets::kInvalidAssetModelIndex
                    ? glm::mat4{1.0f}
                    : prepared[nodeIndex].WorldMatrix;
                while (!chain.empty())
                {
                    const std::uint32_t current = chain.back();
                    chain.pop_back();

                    const glm::mat4 worldMatrix =
                        parentWorld * localMatrices[current];
                    if (!IsFinite(worldMatrix))
                    {
                        return Core::Err<std::vector<PreparedNode>>(
                            Core::ErrorCode::AssetInvalidData);
                    }
                    prepared[current].WorldMatrix = worldMatrix;
                    state[current] = 2u;
                    parentWorld = worldMatrix;
                }
            }
            return prepared;
        }

        [[nodiscard]] Core::Expected<std::vector<PreparedPrimitiveInstance>>
        PreparePrimitiveInstances(
            const Assets::AssetModelScenePayload& model,
            const std::vector<PreparedPrimitive>& primitives,
            const std::vector<PreparedNode>& nodes)
        {
            std::size_t instanceCount = 0u;
            for (const Assets::AssetModelNodePayload& node : model.Nodes)
            {
                instanceCount += node.PrimitiveIndices.size();
            }

            std::vector<PreparedPrimitiveInstance> instances{};
            instances.reserve(instanceCount);
            for (std::size_t nodeIndex = 0u; nodeIndex < model.Nodes.size(); ++nodeIndex)
            {
                for (const std::uint32_t primitiveIndex :
                     model.Nodes[nodeIndex].PrimitiveIndices)
                {
                    if (primitiveIndex >= primitives.size()
                        || nodeIndex >= nodes.size())
                    {
                        return Core::Err<std::vector<PreparedPrimitiveInstance>>(
                            Core::ErrorCode::OutOfRange);
                    }
                    auto worldBounds = ComputeWorldBounds(
                        primitives[primitiveIndex].LocalBounds,
                        nodes[nodeIndex].WorldMatrix);
                    if (!worldBounds.has_value())
                    {
                        return Core::Err<std::vector<PreparedPrimitiveInstance>>(
                            worldBounds.error());
                    }
                    instances.push_back(PreparedPrimitiveInstance{
                        .NodeIndex = static_cast<std::uint32_t>(nodeIndex),
                        .PrimitiveIndex = primitiveIndex,
                        .WorldBounds = *worldBounds,
                    });
                }
            }
            return instances;
        }

        [[nodiscard]] Core::Expected<std::vector<PreparedPrimitive>> PreparePrimitives(
            const Assets::AssetModelScenePayload& model,
            AssetModelSceneHandoffDiagnostics* diagnostics,
            const bool progressiveRawGeometryFirst)
        {
            std::vector<PreparedPrimitive> prepared{};
            prepared.reserve(model.Primitives.size());

            for (std::size_t primitiveIndex = 0u;
                 primitiveIndex < model.Primitives.size();
                 ++primitiveIndex)
            {
                const Assets::AssetModelPrimitivePayload& primitive =
                    model.Primitives[primitiveIndex];
                if (primitive.GeometryKind != Assets::AssetPayloadKind::Mesh)
                {
                    return Core::Err<std::vector<PreparedPrimitive>>(
                        Core::ErrorCode::AssetUnsupportedFormat);
                }
                if (primitive.GeometryPayloadIndex >= model.GeometryPayloads.size())
                {
                    return Core::Err<std::vector<PreparedPrimitive>>(Core::ErrorCode::OutOfRange);
                }

                auto meshPayload = model.GeometryPayloads[primitive.GeometryPayloadIndex]
                    .Read<Geometry::MeshIO::MeshIOResult>();
                if (!meshPayload.has_value())
                {
                    return Core::Err<std::vector<PreparedPrimitive>>(meshPayload.error());
                }

                if (progressiveRawGeometryFirst)
                {
                    auto mesh = BuildRuntimeHalfedgeMeshGeometryOnly(**meshPayload);
                    if (!mesh.has_value())
                    {
                        return Core::Err<std::vector<PreparedPrimitive>>(mesh.error());
                    }
                    auto localBounds = ComputeLocalBounds(*mesh);
                    if (!localBounds.has_value())
                    {
                        return Core::Err<std::vector<PreparedPrimitive>>(
                            localBounds.error());
                    }

                    const bool hasAuthoredTexcoords = MeshPayloadHasValidVertexTexcoords(**meshPayload);
                    prepared.push_back(PreparedPrimitive{
                        .PrimitiveIndex = static_cast<std::uint32_t>(primitiveIndex),
                        .GeometryPayloadIndex = primitive.GeometryPayloadIndex,
                        .MaterialIndex = primitive.MaterialIndex,
                        .VertexCount = primitive.VertexCount,
                        .IndexCount = primitive.IndexCount,
                        .Name = primitive.Name.empty()
                            ? "model-primitive-" + std::to_string(primitiveIndex)
                            : primitive.Name,
                        .Mesh = std::move(*mesh),
                        .LocalBounds = *localBounds,
                        .HasResolvedTexcoords = hasAuthoredTexcoords,
                        .TexcoordProvenance = hasAuthoredTexcoords
                            ? RuntimeMeshResolvedUvProvenance::AuthoredPreserved
                            : RuntimeMeshResolvedUvProvenance::None,
                    });
                    if (diagnostics != nullptr && hasAuthoredTexcoords)
                    {
                        ++diagnostics->AuthoredUvPrimitives;
                    }
                    continue;
                }

                auto materialized = BuildRuntimeHalfedgeMeshMaterialization(**meshPayload);
                if (!materialized.has_value())
                {
                    return Core::Err<std::vector<PreparedPrimitive>>(materialized.error());
                }
                RecordUvMaterializationDiagnostics(
                    diagnostics,
                    materialized->Diagnostics);
                auto localBounds = ComputeLocalBounds(materialized->Mesh);
                if (!localBounds.has_value())
                {
                    return Core::Err<std::vector<PreparedPrimitive>>(
                        localBounds.error());
                }

                prepared.push_back(PreparedPrimitive{
                    .PrimitiveIndex = static_cast<std::uint32_t>(primitiveIndex),
                    .GeometryPayloadIndex = primitive.GeometryPayloadIndex,
                    .MaterialIndex = primitive.MaterialIndex,
                    .VertexCount = primitive.VertexCount,
                    .IndexCount = primitive.IndexCount,
                    .Name = primitive.Name.empty()
                        ? "model-primitive-" + std::to_string(primitiveIndex)
                        : primitive.Name,
                    .Mesh = std::move(materialized->Mesh),
                    .LocalBounds = *localBounds,
                    .HasResolvedTexcoords =
                        materialized->Diagnostics.ResolvedTexcoordsValid,
                    .TexcoordProvenance =
                        materialized->Diagnostics.TexcoordProvenance,
                });
            }

            return prepared;
        }

        [[nodiscard]] Assets::AssetId ResolveTextureReference(
            const Assets::AssetModelTextureReference& reference,
            const std::vector<Assets::AssetId>& embeddedTextureAssets) noexcept
        {
            if (!reference.IsValid()
                || reference.ImageIndex >= embeddedTextureAssets.size())
            {
                return {};
            }
            return embeddedTextureAssets[reference.ImageIndex];
        }

        [[nodiscard]] Graphics::MaterialParams BuildMaterialParams(
            const Assets::AssetModelMaterialPayload& material) noexcept
        {
            Graphics::MaterialParams params{};
            params.BaseColorFactor = glm::vec4(
                material.BaseColorFactor[0],
                material.BaseColorFactor[1],
                material.BaseColorFactor[2],
                material.BaseColorFactor[3]);
            params.MetallicFactor = material.MetallicFactor;
            params.RoughnessFactor = material.RoughnessFactor;
            return params;
        }

        // Neutral lit defaults bound to imported primitives that carry no
        // authored material. StandardPBR (TypeID 0) is lit by default, so the
        // mesh shades using its vertex normals instead of taking the unlit
        // DefaultDebugSurface short-circuit. Slot 0 stays reserved for genuine
        // missing/invalid bindings (GRAPHICS-031).
        [[nodiscard]] Graphics::MaterialParams BuildDefaultLitMaterialParams() noexcept
        {
            Graphics::MaterialParams params{};
            params.BaseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            params.MetallicFactor = 0.0f;
            params.RoughnessFactor = 1.0f;
            params.Flags = Graphics::MaterialFlags::None;
            params.Shading = Graphics::ShadingModel::Lit;
            return params;
        }

        // Lazily create the handoff-scoped default lit material. Returns true
        // when a valid lit slot is available in `state`. Failures are non-fatal:
        // the caller keeps the previous slot-0 fallback so import never fails on
        // this path.
        [[nodiscard]] bool EnsureDefaultLitMaterial(
            Graphics::MaterialSystem& materials,
            AssetModelSceneHandoffState& state,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            if (state.HasDefaultLitMaterial)
            {
                return true;
            }

            const Graphics::MaterialTypeHandle standardType =
                materials.FindType(Graphics::kMaterialTypeName_StandardPBR);
            if (!standardType.IsValid())
            {
                return false;
            }

            auto lease = materials.CreateInstance(
                standardType,
                BuildDefaultLitMaterialParams());
            if (!lease.IsValid())
            {
                return false;
            }

            state.DefaultLitMaterialSlot = materials.GetMaterialSlot(lease.GetHandle());
            state.DefaultLitMaterialLease = std::move(lease);
            state.HasDefaultLitMaterial = true;
            if (diagnostics != nullptr)
            {
                ++diagnostics->MaterialInstancesCreated;
                ++diagnostics->DefaultLitMaterialInstancesCreated;
            }
            return true;
        }

        [[nodiscard]] Graphics::MaterialTextureAssetBindings BuildTextureBindings(
            const Assets::AssetModelMaterialPayload& material,
            const std::vector<Assets::AssetId>& embeddedTextureAssets,
            const GeneratedMaterialTextureAssets& generatedTextureAssets) noexcept
        {
            const Assets::AssetId authoredAlbedo =
                ResolveTextureReference(material.BaseColorTexture, embeddedTextureAssets);
            const Assets::AssetId authoredNormal =
                ResolveTextureReference(material.NormalTexture, embeddedTextureAssets);
            const bool usesGeneratedNormal =
                !authoredNormal.IsValid() && generatedTextureAssets.Normal.IsValid();
            return Graphics::MaterialTextureAssetBindings{
                .Albedo = authoredAlbedo.IsValid() ? authoredAlbedo : generatedTextureAssets.Albedo,
                .Normal = authoredNormal.IsValid() ? authoredNormal : generatedTextureAssets.Normal,
                .MetallicRoughness = ResolveTextureReference(
                    material.MetallicRoughnessTexture,
                    embeddedTextureAssets),
                .Emissive = {},
                .NormalSpace = usesGeneratedNormal
                    ? Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal
                    : Graphics::MaterialNormalTextureSpace::TangentSpaceNormal,
            };
        }

        [[nodiscard]] bool MeshHasVertexProperty(
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const std::string_view propertyName)
        {
            return !propertyName.empty() && mesh.VertexProperties().Exists(propertyName);
        }

        void RecordGeneratedBakeFailure(
            AssetModelSceneHandoffDiagnostics* diagnostics,
            const bool normal)
        {
            if (diagnostics == nullptr)
            {
                return;
            }
            ++diagnostics->GeneratedTextureBakeFailures;
            if (normal)
            {
                ++diagnostics->GeneratedNormalTextureBakeFailures;
            }
            else
            {
                ++diagnostics->GeneratedAlbedoTextureBakeFailures;
            }
        }

        [[nodiscard]] Core::Expected<Assets::AssetId> LoadAndMaybeUploadGeneratedTexture(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            const std::string_view textureAssetPath,
            const Assets::AssetTexture2DPayload& payload,
            const AssetModelSceneHandoffOptions& options,
            AssetModelSceneHandoffState& state,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            auto asset = LoadGeneratedTextureAsset(service, textureAssetPath, payload);
            if (!asset.has_value())
            {
                return Core::Err<Assets::AssetId>(asset.error());
            }

            state.Record.GeneratedTextureAssets.push_back(*asset);
            if (diagnostics != nullptr)
            {
                ++diagnostics->GeneratedTextureAssetsCreated;
            }

            if (!options.RequestGeneratedTextureUploads)
            {
                return *asset;
            }

            auto upload = RequestTextureAssetUpload(
                service,
                cache,
                *asset,
                options.TextureOptions);
            if (upload.has_value())
            {
                if (diagnostics != nullptr)
                {
                    ++diagnostics->GeneratedTextureUploadRequests;
                }
                return *asset;
            }

            if (IsUploadDeferral(upload.error()))
            {
                if (diagnostics != nullptr)
                {
                    ++diagnostics->GeneratedTextureUploadDeferrals;
                }
                return *asset;
            }

            if (diagnostics != nullptr)
            {
                ++diagnostics->GeneratedTextureUploadFailures;
            }
            return Core::Err<Assets::AssetId>(upload.error());
        }

        [[nodiscard]] Core::Expected<Assets::AssetId> TryGenerateMaterialTexture(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            const std::string_view modelPath,
            const std::uint32_t materialIndex,
            const std::string_view semantic,
            const bool normal,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const std::string_view propertyName,
            const AssetModelSceneHandoffOptions& options,
            AssetModelSceneHandoffState& state,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            if (propertyName.empty())
            {
                return Assets::AssetId{};
            }
            if (!normal && !MeshHasVertexProperty(mesh, propertyName))
            {
                return Assets::AssetId{};
            }

            MeshAttributeTextureBakeOptions bakeOptions{};
            bakeOptions.SourcePropertyName = std::string{propertyName};
            bakeOptions.Width = options.GeneratedTextureWidth;
            bakeOptions.Height = options.GeneratedTextureHeight;
            bakeOptions.DebugName =
                std::string{"generated-"} + std::string{semantic} + "-" + std::string{propertyName};

            MeshAttributeTextureBakeResult bake = normal
                ? BakeMeshVertexNormalTexture(mesh, bakeOptions)
                : BakeMeshVertexColorTexture(mesh, bakeOptions);
            if (bake.Status != MeshAttributeTextureBakeStatus::Success)
            {
                RecordGeneratedBakeFailure(diagnostics, normal);
                return Assets::AssetId{};
            }

            bake.Payload.Metadata.SourcePath = std::string{modelPath};
            const std::string generatedPath = BuildGeneratedTextureAssetPath(
                modelPath,
                materialIndex,
                semantic,
                propertyName);
            return LoadAndMaybeUploadGeneratedTexture(
                service,
                cache,
                generatedPath,
                bake.Payload,
                options,
                state,
                diagnostics);
        }

        [[nodiscard]] Core::Expected<std::vector<GeneratedMaterialTextureAssets>>
        GenerateMaterialTextureAssets(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            const Assets::AssetModelScenePayload& model,
            const std::vector<PreparedPrimitive>& primitives,
            const std::string_view modelPath,
            const AssetModelSceneHandoffOptions& options,
            AssetModelSceneHandoffState& state,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            std::vector<GeneratedMaterialTextureAssets> generated(model.Materials.size());
            for (std::size_t materialIndex = 0u;
                 materialIndex < model.Materials.size();
                 ++materialIndex)
            {
                const Assets::AssetModelMaterialPayload& material = model.Materials[materialIndex];
                const bool needsNormal =
                    options.GenerateMissingNormalTextures &&
                    !material.NormalTexture.IsValid() &&
                    options.ObjectSpaceNormalBakeQueue == nullptr;
                const bool needsAlbedo =
                    options.GenerateMissingAlbedoTextures &&
                    !material.BaseColorTexture.IsValid() &&
                    !options.GeneratedAlbedoPropertyName.empty();
                if (!needsNormal && !needsAlbedo)
                {
                    continue;
                }

                for (const PreparedPrimitive& primitive : primitives)
                {
                    if (primitive.MaterialIndex != materialIndex)
                    {
                        continue;
                    }
                    if (!primitive.HasResolvedTexcoords)
                    {
                        continue;
                    }

                    if (needsNormal && !generated[materialIndex].Normal.IsValid())
                    {
                        auto normal = TryGenerateMaterialTexture(
                            service,
                            cache,
                            modelPath,
                            static_cast<std::uint32_t>(materialIndex),
                            "normal",
                            true,
                            primitive.Mesh,
                            options.GeneratedNormalPropertyName,
                            options,
                            state,
                            diagnostics);
                        if (!normal.has_value())
                        {
                            return Core::Err<std::vector<GeneratedMaterialTextureAssets>>(
                                normal.error());
                        }
                        generated[materialIndex].Normal = *normal;
                    }

                    if (needsAlbedo && !generated[materialIndex].Albedo.IsValid())
                    {
                        auto albedo = TryGenerateMaterialTexture(
                            service,
                            cache,
                            modelPath,
                            static_cast<std::uint32_t>(materialIndex),
                            "albedo",
                            false,
                            primitive.Mesh,
                            options.GeneratedAlbedoPropertyName,
                            options,
                            state,
                            diagnostics);
                        if (!albedo.has_value())
                        {
                            return Core::Err<std::vector<GeneratedMaterialTextureAssets>>(
                                albedo.error());
                        }
                        generated[materialIndex].Albedo = *albedo;
                    }

                    if ((!needsNormal || generated[materialIndex].Normal.IsValid()) &&
                        (!needsAlbedo || generated[materialIndex].Albedo.IsValid()))
                    {
                        break;
                    }
                }
            }

            return generated;
        }

        [[nodiscard]] bool BindingHasTextureAsset(
            const Graphics::MaterialTextureAssetBindings& bindings) noexcept
        {
            return bindings.Albedo.IsValid()
                || bindings.Normal.IsValid()
                || bindings.MetallicRoughness.IsValid()
                || bindings.Emissive.IsValid();
        }

        [[nodiscard]] bool BindingReferencesTextureAsset(
            const Graphics::MaterialTextureAssetBindings& bindings,
            const Assets::AssetId id) noexcept
        {
            return id.IsValid()
                && (bindings.Albedo == id
                    || bindings.Normal == id
                    || bindings.MetallicRoughness == id
                    || bindings.Emissive == id);
        }

        [[nodiscard]] bool BindingTextureAssetsReady(
            Graphics::GpuAssetCache& cache,
            const Graphics::MaterialTextureAssetBindings& bindings) noexcept
        {
            const Assets::AssetId ids[] = {
                bindings.Albedo,
                bindings.Normal,
                bindings.MetallicRoughness,
                bindings.Emissive,
            };
            for (const Assets::AssetId id : ids)
            {
                if (id.IsValid()
                    && cache.GetState(id) != Graphics::GpuAssetState::Ready)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Core::Result ResolveMaterialTextureBindingRecord(
            Graphics::MaterialSystem& materials,
            Graphics::GpuAssetCache& cache,
            Graphics::MaterialHandle materialHandle,
            const Graphics::MaterialTextureAssetBindings& bindings)
        {
            if (!BindingHasTextureAsset(bindings))
            {
                return Core::Ok();
            }
            if (!BindingTextureAssetsReady(cache, bindings))
            {
                return Core::Err(Core::ErrorCode::ResourceBusy);
            }
            return materials.ResolveTextureAssetBindings(
                materialHandle,
                bindings,
                cache);
        }

        [[nodiscard]] Core::Expected<std::vector<Assets::AssetId>> LoadEmbeddedTextures(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            const Assets::AssetId modelAsset,
            const Assets::AssetModelScenePayload& model,
            const std::string_view modelPath,
            const AssetModelSceneHandoffOptions& options,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            std::vector<Assets::AssetId> embeddedTextureAssets{};
            embeddedTextureAssets.reserve(model.EmbeddedImages.size());

            for (std::size_t imageIndex = 0u;
                 imageIndex < model.EmbeddedImages.size();
                 ++imageIndex)
            {
                auto child = LoadEmbeddedTextureAsset(
                    service,
                    modelPath,
                    static_cast<std::uint32_t>(imageIndex),
                    model.EmbeddedImages[imageIndex]);
                if (!child.has_value())
                {
                    return Core::Err<std::vector<Assets::AssetId>>(child.error());
                }

                embeddedTextureAssets.push_back(*child);
                if (diagnostics != nullptr)
                {
                    ++diagnostics->EmbeddedTextureAssetsCreated;
                }

                if (options.RequestEmbeddedTextureUploads)
                {
                    auto upload = RequestTextureAssetUpload(
                        service,
                        cache,
                        *child,
                        options.TextureOptions);
                    if (upload.has_value())
                    {
                        if (diagnostics != nullptr)
                        {
                            ++diagnostics->EmbeddedTextureUploadRequests;
                        }
                    }
                    else
                    {
                        if (IsUploadDeferral(upload.error()))
                        {
                            if (diagnostics != nullptr)
                            {
                                ++diagnostics->EmbeddedTextureUploadDeferrals;
                            }
                            continue;
                        }
                        if (diagnostics != nullptr)
                        {
                            ++diagnostics->EmbeddedTextureUploadFailures;
                        }
                        return Core::Err<std::vector<Assets::AssetId>>(upload.error());
                    }
                }
            }

            (void)modelAsset;
            return embeddedTextureAssets;
        }

        [[nodiscard]] Core::Result CreateMaterialRecords(
            Graphics::MaterialSystem& materials,
            Graphics::GpuAssetCache& cache,
            const Assets::AssetModelScenePayload& model,
            const std::vector<Assets::AssetId>& embeddedTextureAssets,
            const std::vector<GeneratedMaterialTextureAssets>& generatedTextureAssets,
            const AssetModelSceneHandoffOptions& options,
            AssetModelSceneHandoffState& state,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            const Graphics::MaterialTypeHandle standardType =
                materials.FindType(Graphics::kMaterialTypeName_StandardPBR);
            if (!standardType.IsValid() && !model.Materials.empty())
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }

            state.Record.Materials.reserve(model.Materials.size());
            state.MaterialLeases.reserve(model.Materials.size());
            for (std::size_t materialIndex = 0u;
                 materialIndex < model.Materials.size();
                 ++materialIndex)
            {
                const Assets::AssetModelMaterialPayload& material = model.Materials[materialIndex];
                const Graphics::MaterialTextureAssetBindings bindings =
                    BuildTextureBindings(
                        material,
                        embeddedTextureAssets,
                        materialIndex < generatedTextureAssets.size()
                            ? generatedTextureAssets[materialIndex]
                            : GeneratedMaterialTextureAssets{});

                auto lease = materials.CreateInstance(standardType, BuildMaterialParams(material));
                if (!lease.IsValid())
                {
                    return Core::Err(Core::ErrorCode::OutOfMemory);
                }

                bool resolved = false;
                if (options.ResolveMaterialTextureBindings)
                {
                    auto resolve = ResolveMaterialTextureBindingRecord(
                        materials,
                        cache,
                        lease.GetHandle(),
                        bindings);
                    resolved = resolve.has_value();
                    if (diagnostics != nullptr)
                    {
                        if (resolved)
                        {
                            ++diagnostics->MaterialTextureBindingsResolved;
                        }
                        else if (resolve.error() == Core::ErrorCode::ResourceBusy)
                        {
                            ++diagnostics->MaterialTextureBindingUploadDeferrals;
                        }
                        else
                        {
                            ++diagnostics->MaterialTextureBindingFailures;
                        }
                    }
                }

                const std::uint32_t slot = materials.GetMaterialSlot(lease.GetHandle());
                state.Record.Materials.push_back(AssetModelSceneMaterialRecord{
                    .MaterialIndex = static_cast<std::uint32_t>(materialIndex),
                    .TextureBindings = bindings,
                    .MaterialSlot = slot,
                    .HasMaterialSlot = true,
                    .TextureBindingsResolved = resolved,
                });
                state.MaterialLeases.push_back(std::move(lease));
                if (diagnostics != nullptr)
                {
                    ++diagnostics->MaterialInstancesCreated;
                }
            }

            return Core::Ok();
        }

        [[nodiscard]] Core::Expected<std::uint64_t> ResolvePendingMaterialTextureBindings(
            Graphics::MaterialSystem& materials,
            Graphics::GpuAssetCache& cache,
            AssetModelSceneHandoffState& state,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            std::uint64_t resolvedCount = 0u;
            const std::size_t materialCount =
                std::min(state.Record.Materials.size(), state.MaterialLeases.size());
            for (std::size_t index = 0u; index < materialCount; ++index)
            {
                AssetModelSceneMaterialRecord& record = state.Record.Materials[index];
                if (record.TextureBindingsResolved)
                {
                    continue;
                }

                if (diagnostics != nullptr)
                {
                    ++diagnostics->MaterialTextureBindingReresolveRequests;
                }

                auto result = ResolveMaterialTextureBindingRecord(
                    materials,
                    cache,
                    state.MaterialLeases[index].GetHandle(),
                    record.TextureBindings);
                if (result.has_value())
                {
                    record.TextureBindingsResolved = true;
                    ++resolvedCount;
                    if (diagnostics != nullptr)
                    {
                        ++diagnostics->MaterialTextureBindingsResolved;
                        ++diagnostics->MaterialTextureBindingReresolveSuccesses;
                    }
                    continue;
                }

                if (diagnostics != nullptr)
                {
                    if (result.error() == Core::ErrorCode::ResourceBusy)
                    {
                        ++diagnostics->MaterialTextureBindingUploadDeferrals;
                    }
                    else
                    {
                        ++diagnostics->MaterialTextureBindingFailures;
                        ++diagnostics->MaterialTextureBindingReresolveFailures;
                        diagnostics->LastFailedAsset = state.Record.ModelAsset;
                        diagnostics->LastError = result.error();
                    }
                }
            }

            return resolvedCount;
        }

        void InvalidateMaterialTextureBindingsForAsset(
            AssetModelSceneHandoffState& state,
            const Assets::AssetId textureAsset,
            AssetModelSceneHandoffDiagnostics& diagnostics)
        {
            for (AssetModelSceneMaterialRecord& material : state.Record.Materials)
            {
                if (!material.TextureBindingsResolved
                    || !BindingReferencesTextureAsset(material.TextureBindings, textureAsset))
                {
                    continue;
                }
                material.TextureBindingsResolved = false;
                ++diagnostics.MaterialTextureBindingReloadInvalidations;
            }
        }

        void DestroyEntities(
            ECS::Scene::Registry& scene,
            const AssetModelSceneHandoffRecord& record)
        {
            auto& raw = scene.Raw();
            const auto detachAllEdges = [&raw](const ECS::EntityHandle entity)
            {
                auto* hierarchy =
                    raw.try_get<ECS::Components::Hierarchy::Component>(entity);
                while (hierarchy != nullptr
                       && raw.valid(hierarchy->FirstChild))
                {
                    ECS::Hierarchy::Detach(raw, hierarchy->FirstChild);
                }
                ECS::Hierarchy::Detach(raw, entity);
            };

            // Detach every incident edge while every referenced hierarchy
            // component is still alive. This also preserves any externally
            // owned entity that was parented under a model node. Node record
            // order is payload order, not a guaranteed post-order.
            for (const AssetModelScenePrimitiveRecord& primitive : record.Primitives)
            {
                if (scene.IsValid(primitive.Entity))
                {
                    detachAllEdges(primitive.Entity);
                }
            }
            for (const AssetModelSceneNodeRecord& node : record.Nodes)
            {
                if (scene.IsValid(node.Entity))
                {
                    detachAllEdges(node.Entity);
                }
            }

            for (const AssetModelScenePrimitiveRecord& primitive : record.Primitives)
            {
                if (scene.IsValid(primitive.Entity))
                {
                    scene.Destroy(primitive.Entity);
                }
            }
            for (const AssetModelSceneNodeRecord& node : record.Nodes)
            {
                if (scene.IsValid(node.Entity))
                {
                    scene.Destroy(node.Entity);
                }
            }
        }

        [[nodiscard]] ProgressiveSlotBinding AuthoredTextureSlot(
            const ProgressiveSlotSemantic semantic,
            const Assets::AssetId asset)
        {
            ProgressiveSlotBinding slot{};
            slot.Semantic = semantic;
            slot.SourceKind = ProgressiveSlotSourceKind::AuthoredTextureAsset;
            slot.AuthoredTexture = asset;
            slot.Readiness = asset.IsValid()
                ? ProgressiveReadinessState::Ready
                : ProgressiveReadinessState::Failed;
            slot.Provenance = ProgressiveGeneratedOutputProvenance::AuthoredAsset;
            return slot;
        }

        [[nodiscard]] ProgressiveSlotBinding UniformColorSlot(
            const Assets::AssetModelMaterialPayload& material)
        {
            ProgressiveSlotBinding slot{};
            slot.Semantic = ProgressiveSlotSemantic::Albedo;
            slot.SourceKind = ProgressiveSlotSourceKind::UniformDefault;
            slot.UniformDefault.Kind = ProgressivePropertyValueKind::Vec4;
            slot.UniformDefault.Vector = glm::vec4{
                material.BaseColorFactor[0],
                material.BaseColorFactor[1],
                material.BaseColorFactor[2],
                material.BaseColorFactor[3],
            };
            slot.Readiness = ProgressiveReadinessState::DefaultValue;
            return slot;
        }

        [[nodiscard]] ProgressiveSlotBinding UniformScalarSlot(
            const ProgressiveSlotSemantic semantic,
            const float value)
        {
            ProgressiveSlotBinding slot{};
            slot.Semantic = semantic;
            slot.SourceKind = ProgressiveSlotSourceKind::UniformDefault;
            slot.UniformDefault.Kind = ProgressivePropertyValueKind::ScalarFloat;
            slot.UniformDefault.Scalar = value;
            slot.Readiness = ProgressiveReadinessState::DefaultValue;
            return slot;
        }

        [[nodiscard]] ProgressiveSlotBinding PendingPropertyBakeSlot(
            const ProgressiveSlotSemantic semantic,
            const std::string& propertyName,
            const ProgressivePropertyValueKind expectedValueKind,
            const std::string_view diagnostic)
        {
            ProgressiveSlotBinding slot{};
            slot.Semantic = semantic;
            slot.SourceKind = ProgressiveSlotSourceKind::PropertyBake;
            slot.Property = ProgressivePropertyBindingDescriptor{
                .Domain = ProgressiveGeometryDomain::MeshVertex,
                .PropertyName = propertyName,
                .ExpectedValueKind = expectedValueKind,
            };
            slot.GeneratedPolicy = ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
            slot.Provenance = ProgressiveGeneratedOutputProvenance::PropertyBinding;
            slot.Readiness = ProgressiveReadinessState::Pending;
            slot.LastDiagnostic = std::string{diagnostic};
            return slot;
        }

        void AttachProgressivePresentationBindings(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const Assets::AssetModelMaterialPayload* material,
            const std::vector<Assets::AssetId>& embeddedTextureAssets,
            const AssetModelSceneHandoffOptions& options,
            const PreparedPrimitive& primitive,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            if (material == nullptr)
            {
                return;
            }

            std::vector<ProgressiveSlotBinding> slots{};
            const Assets::AssetId authoredAlbedo =
                ResolveTextureReference(material->BaseColorTexture, embeddedTextureAssets);
            const Assets::AssetId authoredNormal =
                ResolveTextureReference(material->NormalTexture, embeddedTextureAssets);

            if (authoredAlbedo.IsValid())
            {
                slots.push_back(AuthoredTextureSlot(
                    ProgressiveSlotSemantic::Albedo,
                    authoredAlbedo));
            }
            else if (options.GenerateMissingAlbedoTextures &&
                     MeshHasVertexProperty(primitive.Mesh, options.GeneratedAlbedoPropertyName))
            {
                slots.push_back(PendingPropertyBakeSlot(
                    ProgressiveSlotSemantic::Albedo,
                    options.GeneratedAlbedoPropertyName,
                    ProgressivePropertyValueKind::Any,
                    primitive.HasResolvedTexcoords
                        ? "waiting for vertex color albedo bake"
                        : "waiting for generated UV atlas before vertex color albedo bake"));
            }
            else
            {
                slots.push_back(UniformColorSlot(*material));
            }
            slots.push_back(authoredNormal.IsValid()
                ? AuthoredTextureSlot(ProgressiveSlotSemantic::Normal, authoredNormal)
                : PendingPropertyBakeSlot(
                    ProgressiveSlotSemantic::Normal,
                    options.GeneratedNormalPropertyName,
                    ProgressivePropertyValueKind::Vec3,
                    primitive.HasResolvedTexcoords
                        ? "waiting for vertex normals before normal-map bake"
                        : "waiting for generated UV atlas and vertex normals before normal-map bake"));
            slots.push_back(UniformScalarSlot(
                ProgressiveSlotSemantic::Roughness,
                material->RoughnessFactor));
            slots.push_back(UniformScalarSlot(
                ProgressiveSlotSemantic::Metallic,
                material->MetallicFactor));

            scene.Raw().emplace_or_replace<ProgressivePresentationBindings>(
                entity,
                ProgressivePresentationBindings{
                    .Shape = ProgressiveEntityShape::MeshLeaf,
                    .Lanes = {ProgressiveRenderLaneBinding{
                        .Lane = ProgressiveRenderLane::Surface,
                        .PresentationKey = "mesh.surface",
                    }},
                    .Presentations = {ProgressivePresentationBinding{
                        .Key = "mesh.surface",
                        .Kind = ProgressivePresentationKind::SurfaceMaterial,
                        .Slots = std::move(slots),
                    }},
                });

            if (diagnostics != nullptr)
            {
                ++diagnostics->ProgressivePresentationBindingsCreated;
            }
        }

        [[nodiscard]] bool MeshHasVertexNormals(const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            return mesh.VertexProperties().Exists("v:normal");
        }

        [[nodiscard]] bool MeshHasVertexTexcoords(const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            return mesh.VertexProperties().Exists("v:texcoord");
        }

        [[nodiscard]] std::string_view NormalBakePropertyName(
            const AssetModelSceneHandoffOptions& options) noexcept
        {
            return options.GeneratedNormalPropertyName.empty()
                ? std::string_view{"v:normal"}
                : std::string_view{options.GeneratedNormalPropertyName};
        }

        [[nodiscard]] std::uint64_t
        NextProgressiveBindingGeneration(
            const ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity) noexcept
        {
            const auto* bindings =
                scene.Raw().try_get<ProgressivePresentationBindings>(entity);
            if (bindings == nullptr)
            {
                return 0u;
            }
            const std::uint64_t next = bindings->BindingGeneration + 1u;
            return next == 0u ? 1u : next;
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeRequestBuildResult
        BuildObjectSpaceNormalBakeRequest(
            const ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const AssetModelSceneHandoffOptions& options)
        {
            namespace GS = ECS::Components::GeometrySources;

            Graphics::ObjectSpaceNormalTextureBakeOptions bakeOptions{};
            bakeOptions.Width = options.GeneratedTextureWidth;
            bakeOptions.Height = options.GeneratedTextureHeight;
            bakeOptions.Space = Graphics::NormalTextureSpace::ObjectSpaceNormal;

            const std::uint32_t stableId = StableEntityLookup::ToRenderId(entity);
            const std::uint64_t expectedBindingGeneration =
                NextProgressiveBindingGeneration(scene, entity);
            RuntimeObjectSpaceNormalBakeTarget target{
                .World = options.World,
                .BindingEpoch = options.BindingEpoch,
                .Entity = entity,
                .StableEntityId = stableId,
                .PresentationKey =
                    expectedBindingGeneration != 0u
                        ? std::string{"mesh.surface"}
                        : std::string{},
                .Semantic = ProgressiveSlotSemantic::Normal,
                .ExpectedProgressiveBindingGeneration =
                    expectedBindingGeneration,
            };

            VertexChannelBindingSet channelBindings{};
            const std::string_view normalProperty =
                NormalBakePropertyName(options);
            const VertexChannelBindingSet* channelBindingsPtr = nullptr;
            if (normalProperty != GS::PropertyNames::kNormal)
            {
                channelBindings.Normal.Enabled = true;
                channelBindings.Normal.SourceProperty =
                    std::string{normalProperty};
                channelBindingsPtr = &channelBindings;
            }

            return BuildRuntimeObjectSpaceNormalBakeRequest(
                GS::BuildConstView(scene.Raw(), entity),
                std::move(target),
                bakeOptions,
                channelBindingsPtr);
        }

        void RecordProgressiveNormalBakeDiagnostic(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            std::string diagnostic)
        {
            auto* bindings =
                scene.Raw().try_get<ProgressivePresentationBindings>(entity);
            if (bindings == nullptr)
            {
                return;
            }

            ProgressivePresentationBinding* presentation =
                FindPresentationBinding(*bindings, "mesh.surface");
            if (presentation == nullptr)
            {
                return;
            }

            ProgressiveSlotBinding* slot =
                FindSlotBinding(*presentation, ProgressiveSlotSemantic::Normal);
            if (slot == nullptr ||
                slot->SourceKind != ProgressiveSlotSourceKind::PropertyBake)
            {
                return;
            }

            slot->LastDiagnostic = std::move(diagnostic);
            slot->Readiness = ProgressiveReadinessState::Pending;
            ++bindings->BindingGeneration;
        }

        void WriteDefaultVectorProperty(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const std::string_view propertyName,
            const glm::vec3 value)
        {
            auto* vertices = scene.Raw().try_get<ECS::Components::GeometrySources::Vertices>(entity);
            if (vertices == nullptr)
            {
                return;
            }
            auto property = vertices->Properties.GetOrAdd<glm::vec3>(
                std::string{propertyName},
                value);
            property.Vector().assign(vertices->Properties.Size(), value);
        }

        void WriteDefaultTexcoords(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity)
        {
            auto* vertices = scene.Raw().try_get<ECS::Components::GeometrySources::Vertices>(entity);
            if (vertices == nullptr)
            {
                return;
            }
            auto property = vertices->Properties.GetOrAdd<glm::vec2>(
                "v:texcoord",
                glm::vec2{0.0f});
            property.Vector().assign(vertices->Properties.Size(), glm::vec2{0.0f});
        }

        void MarkProgressiveTextureBakeReady(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const ProgressiveSlotSemantic semantic,
            const std::uint64_t payloadToken,
            std::string diagnostic)
        {
            auto* bindings =
                scene.Raw().try_get<ProgressivePresentationBindings>(entity);
            if (bindings == nullptr)
            {
                return;
            }

            ProgressivePresentationBinding* presentation =
                FindPresentationBinding(*bindings, "mesh.surface");
            if (presentation == nullptr)
            {
                return;
            }

            ProgressiveSlotBinding* slot = FindSlotBinding(*presentation, semantic);
            if (slot == nullptr ||
                slot->SourceKind != ProgressiveSlotSourceKind::PropertyBake)
            {
                return;
            }

            slot->GeneratedTexture = Assets::AssetId{
                static_cast<std::uint32_t>(payloadToken),
                1u};
            slot->Readiness = ProgressiveReadinessState::Ready;
            slot->Provenance =
                ProgressiveGeneratedOutputProvenance::GeneratedTextureAsset;
            slot->LastDiagnostic = std::move(diagnostic);
            ++bindings->BindingGeneration;
        }

        [[nodiscard]] DerivedJobHandle QueueProgressiveNoopJob(
            DerivedJobRegistry& jobs,
            const WorldHandle world,
            const ECS::EntityHandle entity,
            const ProgressiveSlotSemantic semantic,
            std::string name,
            std::uint64_t payloadToken,
            std::move_only_function<Core::Result()> apply)
        {
            const std::uint32_t stableId = StableEntityLookup::ToRenderId(entity);
            DerivedJobDesc desc{};
            desc.Key = DerivedJobKey{
                .EntityId = stableId,
                .Domain = ProgressiveGeometryDomain::MeshVertex,
                .OutputSemantic = semantic,
                .EntityGeneration = static_cast<std::uint64_t>(entity),
                .GeometryGeneration = 1u,
                .SourcePropertyGeneration = 1u,
                .BindingGeneration = 1u,
                .OutputName = name,
            };
            desc.Name = std::move(name);
            desc.Scope = world;
            desc.Execute = [payloadToken]() -> DerivedJobWorkerResult
            {
                return DerivedJobOutput{.PayloadToken = payloadToken};
            };
            desc.ApplyOnMainThread =
                [apply = std::move(apply)](DerivedJobApplyContext&) mutable -> Core::Result
            {
                return apply ? apply() : Core::Ok();
            };
            return jobs.Submit(std::move(desc));
        }

        void QueueProgressiveEnrichmentJobs(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const Assets::AssetModelMaterialPayload* material,
            const PreparedPrimitive& primitive,
            const AssetModelSceneHandoffOptions& options,
            AssetModelSceneHandoffDiagnostics* diagnostics)
        {
            const bool hasProgressiveJobs = options.ProgressiveJobs != nullptr;
            if (!hasProgressiveJobs && options.ObjectSpaceNormalBakeQueue == nullptr)
            {
                return;
            }

            DerivedJobHandle uvJob{};
            DerivedJobHandle normalJob{};

            if (hasProgressiveJobs && !MeshHasVertexTexcoords(primitive.Mesh))
            {
                uvJob = QueueProgressiveNoopJob(
                    *options.ProgressiveJobs,
                    options.World,
                    entity,
                    ProgressiveSlotSemantic::Normal,
                    "generate mesh uv atlas",
                    1001u,
                    [&scene, entity]() -> Core::Result
                    {
                        if (!scene.IsValid(entity))
                        {
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }
                        WriteDefaultTexcoords(scene, entity);
                        return Core::Ok();
                    });
                if (diagnostics != nullptr)
                {
                    ++diagnostics->ProgressiveUvAtlasJobsQueued;
                }
            }

            if (hasProgressiveJobs && !MeshHasVertexNormals(primitive.Mesh))
            {
                normalJob = QueueProgressiveNoopJob(
                    *options.ProgressiveJobs,
                    options.World,
                    entity,
                    ProgressiveSlotSemantic::Normal,
                    "compute mesh vertex normals",
                    1002u,
                    [&scene, entity, propertyName = options.GeneratedNormalPropertyName]() -> Core::Result
                    {
                        if (!scene.IsValid(entity))
                        {
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }
                        WriteDefaultVectorProperty(
                            scene,
                            entity,
                            propertyName.empty() ? std::string_view{"v:normal"} : std::string_view{propertyName},
                            glm::vec3{0.0f, 0.0f, 1.0f});
                        return Core::Ok();
                    });
                if (diagnostics != nullptr)
                {
                    ++diagnostics->ProgressiveNormalJobsQueued;
                }
            }

            const bool materialHasAuthoredNormal =
                material != nullptr && material->NormalTexture.IsValid();
            const bool useRuntimeObjectSpaceNormalBakeQueue =
                material != nullptr &&
                !materialHasAuthoredNormal &&
                options.GenerateMissingNormalTextures &&
                options.ObjectSpaceNormalBakeQueue != nullptr;
            if (useRuntimeObjectSpaceNormalBakeQueue)
            {
                if (hasProgressiveJobs)
                {
                    DerivedJobDesc schedule{};
                    schedule.Key = DerivedJobKey{
                        .EntityId = StableEntityLookup::ToRenderId(entity),
                        .Domain = ProgressiveGeometryDomain::MeshVertex,
                        .OutputSemantic = ProgressiveSlotSemantic::Normal,
                        .EntityGeneration = static_cast<std::uint64_t>(entity),
                        .GeometryGeneration = 1u,
                        .SourcePropertyGeneration = 1u,
                        .BindingGeneration = 1u,
                        .OutputName = "schedule normal GPU bake request",
                    };
                    schedule.Name = "schedule normal GPU bake request";
                    schedule.Scope = options.World;
                    if (uvJob.IsValid())
                    {
                        schedule.DependsOn.push_back(DerivedJobDependency{
                            .Job = uvJob,
                            .Reason = "uv atlas ready",
                        });
                    }
                    if (normalJob.IsValid())
                    {
                        schedule.DependsOn.push_back(DerivedJobDependency{
                            .Job = normalJob,
                            .Reason = "vertex normals ready",
                        });
                    }
                    schedule.Execute = []() -> DerivedJobWorkerResult
                    {
                        return DerivedJobOutput{
                            .PayloadToken = 0u,
                            .Diagnostic = "object-space normal GPU bake request dependencies ready",
                        };
                    };
                    schedule.ApplyOnMainThread =
                        [&scene,
                         entity,
                         queue = options.ObjectSpaceNormalBakeQueue,
                         requestOptions = options](
                            DerivedJobApplyContext&) mutable -> Core::Result
                    {
                        if (!scene.IsValid(entity))
                        {
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }

                        RuntimeObjectSpaceNormalBakeRequestBuildResult build =
                            BuildObjectSpaceNormalBakeRequest(
                                scene,
                                entity,
                                requestOptions);
                        if (!build.Succeeded())
                        {
                            RecordProgressiveNormalBakeDiagnostic(
                                scene,
                                entity,
                                std::move(build.Diagnostic));
                            return Core::Ok();
                        }

                        RuntimeObjectSpaceNormalBakeResult result =
                            queue->Schedule(
                                *build.Request,
                                requestOptions.ObjectSpaceNormalBakeDevice !=
                                        nullptr &&
                                    requestOptions.ObjectSpaceNormalBakeDevice->
                                        IsOperational());
                        RecordProgressiveNormalBakeDiagnostic(
                            scene,
                            entity,
                            std::move(result.Diagnostic));
                        return Core::Ok();
                    };
                    (void)options.ProgressiveJobs->Submit(std::move(schedule));
                    if (diagnostics != nullptr)
                    {
                        ++diagnostics->ProgressiveTextureBakeJobsQueued;
                    }
                }
                else if (MeshHasVertexTexcoords(primitive.Mesh) &&
                         MeshHasVertexProperty(primitive.Mesh, NormalBakePropertyName(options)))
                {
                    RuntimeObjectSpaceNormalBakeRequestBuildResult build =
                        BuildObjectSpaceNormalBakeRequest(
                            scene,
                            entity,
                            options);
                    if (!build.Succeeded())
                    {
                        RecordProgressiveNormalBakeDiagnostic(
                            scene,
                            entity,
                            std::move(build.Diagnostic));
                        return;
                    }
                    RuntimeObjectSpaceNormalBakeResult result =
                        options.ObjectSpaceNormalBakeQueue->Schedule(
                            *build.Request,
                            options.ObjectSpaceNormalBakeDevice != nullptr &&
                                options.ObjectSpaceNormalBakeDevice->
                                    IsOperational());
                    RecordProgressiveNormalBakeDiagnostic(
                        scene,
                        entity,
                        std::move(result.Diagnostic));
                    if (diagnostics != nullptr &&
                        result.Status == RuntimeObjectSpaceNormalBakeStatus::Queued)
                    {
                        ++diagnostics->ProgressiveTextureBakeJobsQueued;
                    }
                }
                else
                {
                    RecordProgressiveNormalBakeDiagnostic(
                        scene,
                        entity,
                        "waiting for resolved UVs and vertex normals before object-space normal GPU bake request");
                }
            }
            else if (hasProgressiveJobs)
            {
                DerivedJobDesc bake{};
                bake.Key = DerivedJobKey{
                    .EntityId = StableEntityLookup::ToRenderId(entity),
                    .Domain = ProgressiveGeometryDomain::MeshVertex,
                    .OutputSemantic = ProgressiveSlotSemantic::Normal,
                    .EntityGeneration = static_cast<std::uint64_t>(entity),
                    .GeometryGeneration = 1u,
                    .SourcePropertyGeneration = 1u,
                    .BindingGeneration = 1u,
                    .OutputName = "bake normal texture",
                };
                bake.Name = "bake normal texture";
                bake.Scope = options.World;
                if (uvJob.IsValid())
                {
                    bake.DependsOn.push_back(DerivedJobDependency{
                        .Job = uvJob,
                        .Reason = "uv atlas ready",
                    });
                }
                if (normalJob.IsValid())
                {
                    bake.DependsOn.push_back(DerivedJobDependency{
                        .Job = normalJob,
                        .Reason = "vertex normals ready",
                    });
                }
                bake.Execute = []() -> DerivedJobWorkerResult
                {
                    return DerivedJobOutput{
                        .PayloadToken = 1003u,
                        .Diagnostic = "normal texture bake completed without upload in CPU contract path",
                    };
                };
                bake.ApplyOnMainThread =
                    [&scene, entity](DerivedJobApplyContext& context) -> Core::Result
                {
                    if (!scene.IsValid(entity))
                    {
                        return Core::Err(Core::ErrorCode::InvalidState);
                    }
                    MarkProgressiveTextureBakeReady(
                        scene,
                        entity,
                        ProgressiveSlotSemantic::Normal,
                        context.Output.PayloadToken,
                        context.Output.Diagnostic);
                    return Core::Ok();
                };
                (void)options.ProgressiveJobs->Submit(std::move(bake));
                if (diagnostics != nullptr)
                {
                    ++diagnostics->ProgressiveTextureBakeJobsQueued;
                }
            }

            const bool materialHasAuthoredAlbedo =
                material != nullptr && material->BaseColorTexture.IsValid();
            if (hasProgressiveJobs &&
                options.GenerateMissingAlbedoTextures &&
                !materialHasAuthoredAlbedo &&
                MeshHasVertexProperty(primitive.Mesh, options.GeneratedAlbedoPropertyName))
            {
                DerivedJobDesc albedoBake{};
                albedoBake.Key = DerivedJobKey{
                    .EntityId = StableEntityLookup::ToRenderId(entity),
                    .Domain = ProgressiveGeometryDomain::MeshVertex,
                    .OutputSemantic = ProgressiveSlotSemantic::Albedo,
                    .EntityGeneration = static_cast<std::uint64_t>(entity),
                    .GeometryGeneration = 1u,
                    .SourcePropertyGeneration = 1u,
                    .BindingGeneration = 1u,
                    .OutputName = "bake albedo texture",
                };
                albedoBake.Name = "bake albedo texture";
                albedoBake.Scope = options.World;
                if (uvJob.IsValid())
                {
                    albedoBake.DependsOn.push_back(DerivedJobDependency{
                        .Job = uvJob,
                        .Reason = "uv atlas ready",
                    });
                }
                albedoBake.Execute = []() -> DerivedJobWorkerResult
                {
                    return DerivedJobOutput{
                        .PayloadToken = 1004u,
                        .Diagnostic = "albedo texture bake completed without upload in CPU contract path",
                    };
                };
                albedoBake.ApplyOnMainThread =
                    [&scene, entity](DerivedJobApplyContext& context) -> Core::Result
                {
                    if (!scene.IsValid(entity))
                    {
                        return Core::Err(Core::ErrorCode::InvalidState);
                    }
                    MarkProgressiveTextureBakeReady(
                        scene,
                        entity,
                        ProgressiveSlotSemantic::Albedo,
                        context.Output.PayloadToken,
                        context.Output.Diagnostic);
                    return Core::Ok();
                };
                (void)options.ProgressiveJobs->Submit(std::move(albedoBake));
                if (diagnostics != nullptr)
                {
                    ++diagnostics->ProgressiveTextureBakeJobsQueued;
                }
            }
        }
    }

    std::string BuildEmbeddedTextureAssetPath(
        const std::string_view modelPath,
        const std::uint32_t imageIndex,
        const Assets::AssetTexture2DPayload& image)
    {
        const std::string_view base = modelPath.empty()
            ? std::string_view{"model-scene"}
            : modelPath;
        return std::string{base}
            + ".embedded-texture-"
            + std::to_string(imageIndex)
            + ExtensionFor(image.Metadata.SourceFormat);
    }

    Core::Expected<Assets::AssetId> LoadEmbeddedTextureAsset(
        Assets::AssetService& service,
        const std::string_view modelPath,
        const std::uint32_t imageIndex,
        const Assets::AssetTexture2DPayload& image)
    {
        const std::string childPath = BuildEmbeddedTextureAssetPath(
            modelPath,
            imageIndex,
            image);
        return service.Load<Assets::AssetTexture2DPayload>(
            childPath,
            [image](std::string_view, Assets::AssetId)
                -> Core::Expected<Assets::AssetTexture2DPayload>
            {
                return image;
            });
    }

    std::string BuildGeneratedTextureAssetPath(
        const std::string_view modelPath,
        const std::uint32_t materialIndex,
        const std::string_view semantic,
        const std::string_view propertyName)
    {
        const std::string_view base = modelPath.empty()
            ? std::string_view{"model-scene"}
            : modelPath;
        return std::string{base}
            + ".generated-texture-material-"
            + std::to_string(materialIndex)
            + "-"
            + SanitizePathToken(semantic)
            + "-"
            + SanitizePathToken(propertyName)
            + ".texture";
    }

    Core::Expected<Assets::AssetId> LoadGeneratedTextureAsset(
        Assets::AssetService& service,
        const std::string_view assetPath,
        const Assets::AssetTexture2DPayload& texture)
    {
        return service.Load<Assets::AssetTexture2DPayload>(
            assetPath,
            [texture](std::string_view, Assets::AssetId)
                -> Core::Expected<Assets::AssetTexture2DPayload>
            {
                return texture;
            });
    }

    Core::Expected<AssetModelSceneHandoffState> MaterializeModelSceneAsset(
        Assets::AssetService& service,
        Graphics::GpuAssetCache& cache,
        ECS::Scene::Registry& scene,
        Graphics::MaterialSystem& materials,
        const Assets::AssetId modelAsset,
        const AssetModelSceneHandoffOptions& options,
        AssetModelSceneHandoffDiagnostics* diagnostics)
    {
        if (diagnostics != nullptr)
        {
            ++diagnostics->ModelSceneMaterializeRequests;
        }

        auto modelSpan = service.Read<Assets::AssetModelScenePayload>(modelAsset);
        if (!modelSpan.has_value())
        {
            RecordFailure(diagnostics, modelAsset, modelSpan.error());
            return Core::Err<AssetModelSceneHandoffState>(modelSpan.error());
        }
        if (modelSpan->size() != 1u)
        {
            RecordFailure(diagnostics, modelAsset, Core::ErrorCode::AssetInvalidData);
            return Core::Err<AssetModelSceneHandoffState>(Core::ErrorCode::AssetInvalidData);
        }

        const Assets::AssetModelScenePayload& model = (*modelSpan)[0];
        if (auto valid = Assets::ValidateAssetModelScenePayload(model); !valid.has_value())
        {
            RecordFailure(diagnostics, modelAsset, valid.error());
            return Core::Err<AssetModelSceneHandoffState>(valid.error());
        }

        auto prepared = PreparePrimitives(
            model,
            diagnostics,
            options.ProgressiveRawGeometryFirst);
        if (!prepared.has_value())
        {
            RecordFailure(diagnostics, modelAsset, prepared.error());
            return Core::Err<AssetModelSceneHandoffState>(prepared.error());
        }
        auto preparedNodes = PrepareNodes(model);
        if (!preparedNodes.has_value())
        {
            RecordFailure(diagnostics, modelAsset, preparedNodes.error());
            return Core::Err<AssetModelSceneHandoffState>(preparedNodes.error());
        }
        auto preparedInstances = PreparePrimitiveInstances(
            model,
            *prepared,
            *preparedNodes);
        if (!preparedInstances.has_value())
        {
            RecordFailure(diagnostics, modelAsset, preparedInstances.error());
            return Core::Err<AssetModelSceneHandoffState>(
                preparedInstances.error());
        }

        std::string modelPath = model.SourcePath;
        if (auto servicePath = service.GetPath(modelAsset); servicePath.has_value())
        {
            modelPath = std::move(*servicePath);
        }

        AssetModelSceneHandoffState state{};
        state.Record.ModelAsset = modelAsset;

        auto embeddedTextures = LoadEmbeddedTextures(
            service,
            cache,
            modelAsset,
            model,
            modelPath,
            options,
            diagnostics);
        if (!embeddedTextures.has_value())
        {
            RecordFailure(diagnostics, modelAsset, embeddedTextures.error());
            return Core::Err<AssetModelSceneHandoffState>(embeddedTextures.error());
        }
        state.Record.EmbeddedTextureAssets = std::move(*embeddedTextures);

        std::vector<GeneratedMaterialTextureAssets> generatedTextureValues{};
        if (options.ProgressiveRawGeometryFirst)
        {
            generatedTextureValues.resize(model.Materials.size());
        }
        else
        {
            auto generatedTextures = GenerateMaterialTextureAssets(
                service,
                cache,
                model,
                *prepared,
                modelPath,
                options,
                state,
                diagnostics);
            if (!generatedTextures.has_value())
            {
                RecordFailure(diagnostics, modelAsset, generatedTextures.error());
                return Core::Err<AssetModelSceneHandoffState>(generatedTextures.error());
            }
            generatedTextureValues = std::move(*generatedTextures);
        }

        if (auto materialRecords = CreateMaterialRecords(
                materials,
                cache,
                model,
                state.Record.EmbeddedTextureAssets,
                generatedTextureValues,
                options,
                state,
                diagnostics);
            !materialRecords.has_value())
        {
            RecordFailure(diagnostics, modelAsset, materialRecords.error());
            return Core::Err<AssetModelSceneHandoffState>(materialRecords.error());
        }

        auto& raw = scene.Raw();
        state.Record.Nodes.reserve(model.Nodes.size());
        std::vector<ECS::EntityHandle> nodeEntities(model.Nodes.size());
        for (std::size_t nodeIndex = 0u; nodeIndex < model.Nodes.size(); ++nodeIndex)
        {
            const Assets::AssetModelNodePayload& node = model.Nodes[nodeIndex];
            const ECS::EntityHandle entity = ECS::Scene::CreateDefault(
                scene,
                node.Name.empty()
                    ? "model-node-" + std::to_string(nodeIndex)
                    : node.Name);
            raw.get<ECS::Components::Transform::Component>(entity) =
                (*preparedNodes)[nodeIndex].LocalTransform;
            raw.get<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
                (*preparedNodes)[nodeIndex].WorldMatrix;
            nodeEntities[nodeIndex] = entity;
            state.Record.Nodes.push_back(AssetModelSceneNodeRecord{
                .Entity = entity,
                .NodeIndex = static_cast<std::uint32_t>(nodeIndex),
            });
            if (diagnostics != nullptr)
            {
                ++diagnostics->NodeEntitiesCreated;
            }
        }

        state.Record.Primitives.reserve(preparedInstances->size());
        std::vector<std::vector<ECS::EntityHandle>> primitiveEntitiesByNode(
            model.Nodes.size());
        for (const PreparedPrimitiveInstance& instance : *preparedInstances)
        {
            PreparedPrimitive& primitive = (*prepared)[instance.PrimitiveIndex];
            const ECS::EntityHandle entity =
                ECS::Scene::CreateDefault(scene, primitive.Name);
            raw.get<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
                (*preparedNodes)[instance.NodeIndex].WorldMatrix;
            raw.emplace_or_replace<Graphics::Components::RenderSurface>(entity);
            raw.emplace_or_replace<ECS::Components::Culling::Local::Bounds>(
                entity,
                primitive.LocalBounds);
            raw.emplace_or_replace<ECS::Components::Culling::World::Bounds>(
                entity,
                instance.WorldBounds);
            ECS::Components::GeometrySources::PopulateFromMesh(
                raw,
                entity,
                primitive.Mesh);
            const Assets::AssetModelMaterialPayload* material =
                primitive.MaterialIndex < model.Materials.size()
                    ? &model.Materials[primitive.MaterialIndex]
                    : nullptr;
            if (options.ProgressiveRawGeometryFirst)
            {
                AttachProgressivePresentationBindings(scene,
                                                       entity,
                                                       material,
                                                       state.Record.EmbeddedTextureAssets,
                                                       options,
                                                       primitive,
                                                       diagnostics);
                QueueProgressiveEnrichmentJobs(scene,
                                                entity,
                                                material,
                                                primitive,
                                                options,
                                                diagnostics);
            }
            else if (options.ObjectSpaceNormalBakeQueue != nullptr)
            {
                QueueProgressiveEnrichmentJobs(scene,
                                                entity,
                                                material,
                                                primitive,
                                                options,
                                                diagnostics);
            }

            std::uint32_t materialSlot = Graphics::kDefaultMaterialSlotIndex;
            bool hasMaterialSlot = false;
            if (primitive.MaterialIndex < state.Record.Materials.size())
            {
                const AssetModelSceneMaterialRecord& material =
                    state.Record.Materials[primitive.MaterialIndex];
                materialSlot = material.MaterialSlot;
                hasMaterialSlot = material.HasMaterialSlot;
            }
            else if (EnsureDefaultLitMaterial(materials, state, diagnostics))
            {
                // No authored material for this primitive: bind a neutral lit
                // default so the mesh shades, rather than the unlit slot-0
                // DefaultDebugSurface fallback.
                materialSlot = state.DefaultLitMaterialSlot;
                hasMaterialSlot = true;
                if (diagnostics != nullptr)
                {
                    ++diagnostics->MaterialLessPrimitivesAssignedDefaultLit;
                }
            }

            state.Record.Primitives.push_back(AssetModelScenePrimitiveRecord{
                .Entity = entity,
                .NodeIndex = instance.NodeIndex,
                .PrimitiveIndex = primitive.PrimitiveIndex,
                .GeometryPayloadIndex = primitive.GeometryPayloadIndex,
                .MaterialIndex = primitive.MaterialIndex,
                .MaterialSlot = materialSlot,
                .HasMaterialSlot = hasMaterialSlot,
            });
            primitiveEntitiesByNode[instance.NodeIndex].push_back(entity);
            if (diagnostics != nullptr)
            {
                ++diagnostics->PrimitiveEntitiesCreated;
                if (options.ProgressiveRawGeometryFirst)
                {
                    ++diagnostics->ProgressiveRawPrimitiveEntitiesPublished;
                }
            }
        }

        // Attach in reverse because Structure::AttachToParent head-inserts.
        // The resulting sibling order is authored child nodes followed by the
        // node's primitive instances, each group retaining payload order.
        for (std::size_t nodeIndex = 0u; nodeIndex < model.Nodes.size(); ++nodeIndex)
        {
            auto& parentHierarchy =
                raw.get<ECS::Components::Hierarchy::Component>(
                    nodeEntities[nodeIndex]);
            const auto& primitiveChildren = primitiveEntitiesByNode[nodeIndex];
            for (auto primitive = primitiveChildren.rbegin();
                 primitive != primitiveChildren.rend();
                 ++primitive)
            {
                auto& childHierarchy =
                    raw.get<ECS::Components::Hierarchy::Component>(*primitive);
                ECS::Hierarchy::Structure::AttachToParent(
                    raw,
                    *primitive,
                    childHierarchy,
                    nodeEntities[nodeIndex],
                    parentHierarchy);
            }

            const auto& childNodes = model.Nodes[nodeIndex].ChildNodeIndices;
            for (auto child = childNodes.rbegin();
                 child != childNodes.rend();
                 ++child)
            {
                auto& childHierarchy =
                    raw.get<ECS::Components::Hierarchy::Component>(
                        nodeEntities[*child]);
                ECS::Hierarchy::Structure::AttachToParent(
                    raw,
                    nodeEntities[*child],
                    childHierarchy,
                    nodeEntities[nodeIndex],
                    parentHierarchy);
            }
        }

        if (diagnostics != nullptr)
        {
            ++diagnostics->ModelSceneMaterializeSuccesses;
            diagnostics->LastError = Core::ErrorCode::Success;
        }
        return state;
    }

    struct AssetModelSceneHandoff::Impl
    {
        Assets::AssetService& Service;
        Graphics::GpuAssetCache& Cache;
        ECS::Scene::Registry& Scene;
        Graphics::IRenderer& Renderer;
        AssetModelSceneHandoffOptions Options{};
        AssetModelSceneHandoffDiagnostics Diagnostics{};
        Assets::AssetEventBus::ListenerToken Token{Assets::AssetEventBus::InvalidToken};
        std::unordered_map<Assets::AssetId, RuntimeModelSceneRecord, Assets::AssetIdHash> Records{};

        Impl(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            ECS::Scene::Registry& scene,
            Graphics::IRenderer& renderer,
            AssetModelSceneHandoffOptions options)
            : Service(service)
            , Cache(cache)
            , Scene(scene)
            , Renderer(renderer)
            , Options(options)
        {
            Token = Service.SubscribeAll(
                [this](const Assets::AssetId id, const Assets::AssetEvent event)
                {
                    Handle(id, event);
                });
        }

        ~Impl()
        {
            if (Token != Assets::AssetEventBus::InvalidToken)
            {
                Service.UnsubscribeAll(Token);
                Token = Assets::AssetEventBus::InvalidToken;
            }
            for (auto& [_, record] : Records)
            {
                DestroyEntities(Scene, record.State.Record);
            }
            Records.clear();
        }

        [[nodiscard]] bool IsBindingValid() const
        {
            return !Options.BindingValid || Options.BindingValid();
        }

        [[nodiscard]] Core::Result MaterializeReadyModelScene(const Assets::AssetId id)
        {
            if (!IsBindingValid())
                return Core::Err(Core::ErrorCode::InvalidState);

            auto state = MaterializeModelSceneAsset(
                Service,
                Cache,
                Scene,
                Renderer.GetMaterialSystem(),
                id,
                Options,
                &Diagnostics);
            if (!state.has_value())
            {
                return Core::Err(state.error());
            }

            RuntimeModelSceneRecord replacement{.State = std::move(*state)};
            if (auto it = Records.find(id); it != Records.end())
            {
                RuntimeModelSceneRecord previous = std::move(it->second);
                it->second = std::move(replacement);
                DestroyEntities(Scene, previous.State.Record);
            }
            else
            {
                Records.emplace(id, std::move(replacement));
            }
            return Core::Ok();
        }

        [[nodiscard]] Core::Expected<std::uint64_t> ResolvePendingMaterialTextureBindings()
        {
            if (!IsBindingValid())
            {
                return Core::Err<std::uint64_t>(
                    Core::ErrorCode::InvalidState);
            }

            std::uint64_t resolvedCount = 0u;
            for (auto& [_, record] : Records)
            {
                auto resolved = ::Extrinsic::Runtime::ResolvePendingMaterialTextureBindings(
                    Renderer.GetMaterialSystem(),
                    Cache,
                    record.State,
                    &Diagnostics);
                if (!resolved.has_value())
                {
                    return Core::Err<std::uint64_t>(resolved.error());
                }
                resolvedCount += *resolved;
            }
            return resolvedCount;
        }

        void InvalidateMaterialTextureBindingsForAsset(const Assets::AssetId id)
        {
            for (auto& [_, record] : Records)
            {
                ::Extrinsic::Runtime::InvalidateMaterialTextureBindingsForAsset(
                    record.State,
                    id,
                    Diagnostics);
            }
        }

        void HandleReady(const Assets::AssetId id)
        {
            ++Diagnostics.ReadyEventsObserved;

            auto model = Service.Read<Assets::AssetModelScenePayload>(id);
            if (!model.has_value())
            {
                if (!IsTypeMismatch(model.error()))
                {
                    return;
                }

                auto texture = Service.Read<Assets::AssetTexture2DPayload>(id);
                if (texture.has_value())
                {
                    ++Diagnostics.NonModelSceneReadyEvents;
                    static_cast<void>(ResolvePendingMaterialTextureBindings());
                    return;
                }
                ++Diagnostics.NonModelSceneReadyEvents;
                return;
            }

            ++Diagnostics.ModelSceneReadyEvents;
            static_cast<void>(MaterializeReadyModelScene(id));
        }

        void Handle(const Assets::AssetId id, const Assets::AssetEvent event)
        {
            if (!IsBindingValid())
                return;

            if (event == Assets::AssetEvent::Ready)
            {
                HandleReady(id);
            }
            else if (event == Assets::AssetEvent::Reloaded)
            {
                auto model = Service.Read<Assets::AssetModelScenePayload>(id);
                if (!model.has_value())
                {
                    InvalidateMaterialTextureBindingsForAsset(id);
                }
                // Reloaded announces invalidation/queued work. The paired
                // Ready event is the only point at which a model replacement
                // is complete and may be materialized transactionally.
            }
            else if (event == Assets::AssetEvent::Destroyed)
            {
                if (auto it = Records.find(id); it != Records.end())
                {
                    DestroyEntities(Scene, it->second.State.Record);
                    Records.erase(it);
                }
                InvalidateMaterialTextureBindingsForAsset(id);
            }
        }
    };

    AssetModelSceneHandoff::AssetModelSceneHandoff(
        Assets::AssetService& service,
        Graphics::GpuAssetCache& cache,
        ECS::Scene::Registry& scene,
        Graphics::IRenderer& renderer,
        AssetModelSceneHandoffOptions options)
        : m_Impl(std::make_unique<Impl>(
            service,
            cache,
            scene,
            renderer,
            options))
    {
    }

    AssetModelSceneHandoff::~AssetModelSceneHandoff() = default;

    bool AssetModelSceneHandoff::IsSubscribed() const noexcept
    {
        return m_Impl != nullptr
            && m_Impl->Token != Assets::AssetEventBus::InvalidToken;
    }

    AssetModelSceneHandoffDiagnostics
    AssetModelSceneHandoff::GetDiagnostics() const noexcept
    {
        return m_Impl != nullptr
            ? m_Impl->Diagnostics
            : AssetModelSceneHandoffDiagnostics{};
    }

    const AssetModelSceneHandoffRecord* AssetModelSceneHandoff::FindRecord(
        const Assets::AssetId modelAsset) const noexcept
    {
        if (m_Impl == nullptr)
        {
            return nullptr;
        }
        const auto it = m_Impl->Records.find(modelAsset);
        return it != m_Impl->Records.end()
            ? &it->second.State.Record
            : nullptr;
    }

    Core::Result AssetModelSceneHandoff::MaterializeReadyModelScene(
        const Assets::AssetId modelAsset)
    {
        if (m_Impl == nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return m_Impl->MaterializeReadyModelScene(modelAsset);
    }

    Core::Expected<std::uint64_t>
    AssetModelSceneHandoff::ResolvePendingMaterialTextureBindings()
    {
        if (m_Impl == nullptr)
        {
            return Core::Err<std::uint64_t>(Core::ErrorCode::InvalidState);
        }
        return m_Impl->ResolvePendingMaterialTextureBindings();
    }
}
