module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.AssetWorkflowModelMaterialization;

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
import Extrinsic.Runtime.AssetWorkflowGeometryMaterialization;
import Extrinsic.Runtime.AssetWorkflowTextureResidency;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.TextureBakeModule;
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
            std::uint32_t UvAtlasWidth{0u};
            std::uint32_t UvAtlasHeight{0u};
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

        void RecordFailure(
            AssetWorkflowModelMaterializationDiagnostics* diagnostics,
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
            AssetWorkflowModelMaterializationDiagnostics* diagnostics,
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
            diagnostics->UvAtlasGpuSplitVertices += uvDiagnostics.GpuSplitVertexCount;
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
            AssetWorkflowModelMaterializationDiagnostics* diagnostics,
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

                // An unusable atlas must not discard otherwise renderable geometry.
                auto materialized = BuildRuntimeHalfedgeMeshMaterialization(
                    **meshPayload, RuntimeMeshMaterializationOptions{
                        .UvResolution = {.FailurePolicy = RuntimeMeshUvFailurePolicy::Optional}});
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
                    .UvAtlasWidth = materialized->Diagnostics.AtlasWidth,
                    .UvAtlasHeight = materialized->Diagnostics.AtlasHeight,
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

        [[nodiscard]] bool MeshHasVertexProperty(
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const std::string_view propertyName)
        {
            return !propertyName.empty() && mesh.VertexProperties().Exists(propertyName);
        }

        [[nodiscard]] Core::Expected<std::vector<Assets::AssetId>> LoadEmbeddedTextures(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            const Assets::AssetModelScenePayload& model,
            const std::string_view modelPath,
            const AssetWorkflowModelMaterializationOptions& options,
            AssetWorkflowModelMaterializationDiagnostics* diagnostics)
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
                        if (IsTextureUploadDeferred(upload.error()))
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

            return embeddedTextureAssets;
        }

        void DestroyEntities(
            ECS::Scene::Registry& scene,
            const AssetWorkflowModelMaterializationRecord& record)
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
            for (const AssetWorkflowModelPrimitiveRecord& primitive : record.Primitives)
            {
                if (scene.IsValid(primitive.Entity))
                {
                    detachAllEdges(primitive.Entity);
                }
            }
            for (const AssetWorkflowModelNodeRecord& node : record.Nodes)
            {
                if (scene.IsValid(node.Entity))
                {
                    detachAllEdges(node.Entity);
                }
            }

            for (const AssetWorkflowModelPrimitiveRecord& primitive : record.Primitives)
            {
                if (scene.IsValid(primitive.Entity))
                {
                    scene.Destroy(primitive.Entity);
                }
            }
            for (const AssetWorkflowModelNodeRecord& node : record.Nodes)
            {
                if (scene.IsValid(node.Entity))
                {
                    scene.Destroy(node.Entity);
                }
            }
        }

        [[nodiscard]] GeometryPresentationSlotRecipe AuthoredTextureSlot(
            const GeometryPresentationSlotSemantic semantic,
            const Assets::AssetId asset)
        {
            GeometryPresentationSlotRecipe slot{};
            slot.Semantic = semantic;
            slot.SourceKind = GeometryPresentationSourceKind::AuthoredTextureAsset;
            slot.TextureAsset = asset;
            return slot;
        }

        [[nodiscard]] GeometryPresentationSlotRecipe UniformColorSlot(
            const Assets::AssetModelMaterialPayload& material)
        {
            GeometryPresentationSlotRecipe slot{};
            slot.Semantic = GeometryPresentationSlotSemantic::Albedo;
            slot.SourceKind = GeometryPresentationSourceKind::UniformDefault;
            slot.UniformDefault.Kind = Geometry::PropertyValueKind::Vec4;
            slot.UniformDefault.Vector = glm::vec4{
                material.BaseColorFactor[0],
                material.BaseColorFactor[1],
                material.BaseColorFactor[2],
                material.BaseColorFactor[3],
            };
            return slot;
        }

        [[nodiscard]] GeometryPresentationSlotRecipe UniformScalarSlot(
            const GeometryPresentationSlotSemantic semantic,
            const float value)
        {
            GeometryPresentationSlotRecipe slot{};
            slot.Semantic = semantic;
            slot.SourceKind = GeometryPresentationSourceKind::UniformDefault;
            slot.UniformDefault.Kind = Geometry::PropertyValueKind::Float;
            slot.UniformDefault.Scalar = value;
            return slot;
        }

        [[nodiscard]] GeometryPresentationSlotRecipe PendingPropertyBakeSlot(
            const GeometryPresentationSlotSemantic semantic,
            const std::string& propertyName,
            std::string outputName,
            const GeometryPropertyValueKindFilter expectedValueKind,
            const std::string_view diagnostic,
            GeometryPresentationRuntimeState& runtimeState)
        {
            GeometryPresentationSlotRecipe slot{};
            slot.Semantic = semantic;
            slot.SourceKind = GeometryPresentationSourceKind::PropertyBake;
            slot.Property = GeometryPropertyRef{
                .Domain = GeometryElementDomain::MeshVertex,
                .Name = propertyName,
                .ValueKind = expectedValueKind.value_or(
                    Geometry::PropertyValueKind::Unknown),
            };
            slot.GeneratedOutputName = std::move(outputName);
            slot.GeneratedPolicy = GeometryGeneratedOutputPolicy::DeterministicChildAsset;
            runtimeState.Slots.push_back(GeometryPresentationSlotStatus{
                .PresentationKey = "mesh.surface",
                .Semantic = semantic,
                .Readiness = GeometryPresentationReadiness::Pending,
                .GeneratedOutputName = slot.GeneratedOutputName,
                .Provenance = GeometryPresentationProvenance::PropertyBinding,
                .Diagnostic = std::string{diagnostic},
            });
            return slot;
        }

        [[nodiscard]] GeometryPresentationSlotRecipe PropertyBufferSlot(
            const GeometryPresentationSlotSemantic semantic,
            const std::string& propertyName,
            const GeometryPropertyValueKindFilter expectedValueKind,
            GeometryPresentationRuntimeState& runtimeState)
        {
            GeometryPresentationSlotRecipe slot{};
            slot.Semantic = semantic;
            slot.SourceKind = GeometryPresentationSourceKind::PropertyBuffer;
            slot.Property = GeometryPropertyRef{
                .Domain = GeometryElementDomain::MeshVertex,
                .Name = propertyName,
                .ValueKind = expectedValueKind.value_or(
                    Geometry::PropertyValueKind::Unknown),
            };
            runtimeState.Slots.push_back(GeometryPresentationSlotStatus{
                .PresentationKey = "mesh.surface",
                .Semantic = semantic,
                .Readiness = GeometryPresentationReadiness::Ready,
                .Provenance = GeometryPresentationProvenance::PropertyBuffer,
                .Diagnostic = "property-buffer fallback is active",
            });
            return slot;
        }

        void AttachGeometryPresentationRecipe(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const Assets::AssetModelMaterialPayload* material,
            const std::vector<Assets::AssetId>& embeddedTextureAssets,
            const AssetWorkflowModelMaterializationOptions& options,
            const PreparedPrimitive& primitive,
            AssetWorkflowModelMaterializationDiagnostics* diagnostics)
        {
            if (material == nullptr)
            {
                return;
            }

            std::vector<GeometryPresentationSlotRecipe> slots{};
            GeometryPresentationRuntimeState runtimeState{};
            const Assets::AssetId authoredAlbedo =
                ResolveTextureReference(material->BaseColorTexture, embeddedTextureAssets);
            const Assets::AssetId authoredNormal =
                ResolveTextureReference(material->NormalTexture, embeddedTextureAssets);

            if (authoredAlbedo.IsValid())
            {
                slots.push_back(AuthoredTextureSlot(
                    GeometryPresentationSlotSemantic::Albedo,
                    authoredAlbedo));
            }
            else if (options.GenerateMissingAlbedoTextures &&
                     MeshHasVertexProperty(primitive.Mesh, options.GeneratedAlbedoPropertyName))
            {
                slots.push_back(
                    options.TextureBake != nullptr
                        ? PendingPropertyBakeSlot(
                    GeometryPresentationSlotSemantic::Albedo,
                    options.GeneratedAlbedoPropertyName,
                              "generated-albedo",
                    std::nullopt,
                    primitive.HasResolvedTexcoords
                        ? "waiting for vertex color albedo bake"
                        : "waiting for generated UV atlas before vertex color albedo bake",
                    runtimeState)
                        : PropertyBufferSlot(
                              GeometryPresentationSlotSemantic::Albedo,
                              options.GeneratedAlbedoPropertyName,
                              std::nullopt,
                              runtimeState));
            }
            else
            {
                slots.push_back(UniformColorSlot(*material));
            }
            slots.push_back(authoredNormal.IsValid()
                ? AuthoredTextureSlot(GeometryPresentationSlotSemantic::Normal, authoredNormal)
                : options.GenerateMissingNormalTextures &&
                                    options.TextureBake != nullptr
                                ? PendingPropertyBakeSlot(
                    GeometryPresentationSlotSemantic::Normal,
                    options.GeneratedNormalPropertyName,
                                      "generated-normal",
                    Geometry::PropertyValueKind::Vec3,
                    primitive.HasResolvedTexcoords
                        ? "waiting for vertex normals before normal-map bake"
                        : "waiting for generated UV atlas and vertex normals before normal-map bake",
                    runtimeState)
                                : PropertyBufferSlot(
                                      GeometryPresentationSlotSemantic::Normal,
                                      options.GeneratedNormalPropertyName,
                                      Geometry::PropertyValueKind::Vec3,
                                      runtimeState));
            slots.push_back(UniformScalarSlot(
                GeometryPresentationSlotSemantic::Roughness,
                material->RoughnessFactor));
            slots.push_back(UniformScalarSlot(
                GeometryPresentationSlotSemantic::Metallic,
                material->MetallicFactor));

            scene.Raw().emplace_or_replace<GeometryPresentationRecipe>(
                entity,
                GeometryPresentationRecipe{
                    .Shape = GeometryPresentationShape::Mesh,
                    .Lanes = {GeometryPresentationLaneRecipe{
                        .Lane = GeometryRenderLane::Surface,
                        .PresentationKey = "mesh.surface",
                    }},
                    .Presentations = {GeometryPresentationBindingRecipe{
                        .Key = "mesh.surface",
                        .Kind = GeometryPresentationKind::SurfaceMaterial,
                        .Slots = std::move(slots),
                    }},
                });
            scene.Raw().emplace_or_replace<GeometryPresentationRuntimeState>(
                entity,
                std::move(runtimeState));

            if (diagnostics != nullptr)
            {
                ++diagnostics->GeometryPresentationRecipesCreated;
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
            const AssetWorkflowModelMaterializationOptions& options) noexcept
        {
            return options.GeneratedNormalPropertyName.empty()
                ? std::string_view{"v:normal"}
                : std::string_view{options.GeneratedNormalPropertyName};
        }

        [[nodiscard]] PropertyTextureBakeRequest
        BuildGeneratedPropertyTextureBakeRequest(
            const ECS::EntityHandle entity,
            const AssetWorkflowModelMaterializationOptions& options,
            const std::string_view propertyName,
            const Geometry::PropertyValueKind valueKind,
            const PropertyTextureBakeEncoding encoding,
            std::string outputName)
        {
            return PropertyTextureBakeRequest{
                .World = options.World,
                .StableEntityId =
                    StableEntityLookup::ToRenderId(entity),
                .Source = GeometryPropertyRef{
                    .Domain = GeometryElementDomain::MeshVertex,
                    .Name = std::string{propertyName},
                    .ValueKind = valueKind,
                },
                .Storage = PropertyTextureBakeStorage::EncodedRgba,
                .Encoding = encoding,
                .Width = options.GeneratedTextureWidth,
                .Height = options.GeneratedTextureHeight,
                .PaddingTexels =
                    options.GeneratedTexturePaddingTexels,
                .OutputName = std::move(outputName),
            };
        }

        [[nodiscard]] PropertyTextureBakeResult
        ScheduleGeneratedPropertyTextureBake(
            TextureBakeService& textureBake,
            const ECS::EntityHandle entity,
            const AssetWorkflowModelMaterializationOptions& options,
            const std::string_view propertyName,
            const Geometry::PropertyValueKind valueKind,
            const PropertyTextureBakeEncoding encoding,
            std::string outputName)
        {
            return textureBake.Bake(
                BuildGeneratedPropertyTextureBakeRequest(
                    entity,
                    options,
                    propertyName,
                    valueKind,
                    encoding,
                    std::move(outputName)));
        }

        void RecordProgressiveTextureBakeDiagnostic(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const GeometryPresentationSlotSemantic semantic,
            std::string diagnostic)
        {
            auto* bindings =
                scene.Raw().try_get<GeometryPresentationRecipe>(entity);
            if (bindings == nullptr)
            {
                return;
            }

            GeometryPresentationBindingRecipe* presentation =
                FindGeometryPresentationBinding(*bindings, "mesh.surface");
            if (presentation == nullptr)
            {
                return;
            }

            GeometryPresentationSlotRecipe* slot =
                FindGeometryPresentationSlot(*presentation, semantic);
            if (slot == nullptr ||
                slot->SourceKind != GeometryPresentationSourceKind::PropertyBake)
            {
                return;
            }

            auto* state = scene.Raw().try_get<GeometryPresentationRuntimeState>(
                entity);
            if (state == nullptr)
                return;
            GeometryPresentationSlotStatus* status =
                FindGeometryPresentationSlotStatus(
                    *state,
                    "mesh.surface",
                    semantic);
            if (status == nullptr)
            {
                state->Slots.push_back(GeometryPresentationSlotStatus{
                    .PresentationKey = "mesh.surface",
                    .Semantic = semantic,
                });
                status = &state->Slots.back();
            }
            status->Diagnostic = std::move(diagnostic);
            status->Readiness = GeometryPresentationReadiness::Pending;
            status->Provenance =
                GeometryPresentationProvenance::PropertyBinding;
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

        // The retired `DerivedJobOutput` carried a payload token and a
        // diagnostic on every derived job. `JobService` results are typed, so
        // the enrichment chain declares the one record its apply bodies read.
        // Jobs whose apply ignores the payload still return a populated
        // envelope, because an empty envelope is how `JobService` reports a
        // dropped job.
        struct ProgressiveEnrichmentResult
        {
            std::uint64_t PayloadToken{0u};
            std::string Diagnostic{};
        };

        // RUNTIME-194 Slice B5b: the enrichment jobs no longer carry a
        // `DerivedJobKey`. Nothing here looked the jobs up by entity, semantic,
        // or generation — the key existed only for the retired registry's
        // dedup/enumeration — so identity stays with the consumer that has a
        // reason to hold it, per the Slice B5 decision.
        [[nodiscard]] JobToken QueueProgressiveNoopJob(
            JobService& jobs,
            const WorldHandle world,
            std::string name,
            std::uint64_t payloadToken,
            std::move_only_function<Core::Result()> apply)
        {
            JobDesc desc{};
            desc.DebugName = std::move(name);
            desc.Scope = world;
            desc.Kind = RuntimeTaskKinds::GeometryProcess;
            desc.Work = [payloadToken](const JobCancellation&) -> JobResultEnvelope
            {
                return JobResultEnvelope::Make<ProgressiveEnrichmentResult>(
                    ProgressiveEnrichmentResult{.PayloadToken = payloadToken});
            };
            desc.PublishCompletion =
                [apply = std::move(apply)](
                    KernelEventBus&,
                    const JobResultEnvelope&) mutable -> bool
            {
                return !apply || apply().has_value();
            };
            return jobs.Submit(std::move(desc));
        }

        void QueueProgressiveEnrichmentJobs(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const Assets::AssetModelMaterialPayload* material,
            const PreparedPrimitive& primitive,
            const AssetWorkflowModelMaterializationOptions& options,
            AssetWorkflowModelMaterializationDiagnostics* diagnostics)
        {
            const bool hasProgressiveJobs = options.ProgressiveJobs != nullptr;
            if (!hasProgressiveJobs && options.TextureBake == nullptr)
            {
                return;
            }

            AssetWorkflowModelMaterializationOptions bakeOptions = options;
            if (bakeOptions.GeneratedTextureWidth == 0u)
            {
                bakeOptions.GeneratedTextureWidth =
                    primitive.UvAtlasWidth != 0u
                        ? primitive.UvAtlasWidth
                        : 1024u;
            }
            if (bakeOptions.GeneratedTextureHeight == 0u)
            {
                bakeOptions.GeneratedTextureHeight =
                    primitive.UvAtlasHeight != 0u
                        ? primitive.UvAtlasHeight
                        : 1024u;
            }

            JobToken uvJob{};
            JobToken normalJob{};

            if (hasProgressiveJobs && !MeshHasVertexTexcoords(primitive.Mesh))
            {
                uvJob = QueueProgressiveNoopJob(
                    *options.ProgressiveJobs,
                    options.World,
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
            const bool useTextureBake =
                material != nullptr &&
                !materialHasAuthoredNormal &&
                options.GenerateMissingNormalTextures &&
                options.TextureBake != nullptr;
            if (useTextureBake)
            {
                if (hasProgressiveJobs)
                {
                    JobDesc schedule{};
                    schedule.DebugName = "schedule normal GPU bake request";
                    schedule.Scope = options.World;
                    schedule.Kind = RuntimeTaskKinds::GeometryProcess;
                    if (uvJob.IsValid())
                    {
                        schedule.DependsOn.push_back(JobDependency{
                            .Job = uvJob,
                            .Reason = "uv atlas ready",
                        });
                    }
                    if (normalJob.IsValid())
                    {
                        schedule.DependsOn.push_back(JobDependency{
                            .Job = normalJob,
                            .Reason = "vertex normals ready",
                        });
                    }
                    schedule.Work = [](const JobCancellation&) -> JobResultEnvelope
                    {
                        return JobResultEnvelope::Make<ProgressiveEnrichmentResult>(
                            ProgressiveEnrichmentResult{
                                .PayloadToken = 0u,
                                .Diagnostic = "property texture bake request dependencies ready",
                            });
                    };
                    schedule.PublishCompletion =
                        [&scene,
                         entity,
                         textureBake = options.TextureBake,
                         requestOptions = bakeOptions](
                            KernelEventBus&,
                            const JobResultEnvelope&) mutable -> bool
                    {
                        if (!scene.IsValid(entity))
                        {
                            return false;
                        }

                        PropertyTextureBakeResult result =
                            ScheduleGeneratedPropertyTextureBake(
                                *textureBake,
                                entity,
                                requestOptions,
                                NormalBakePropertyName(requestOptions),
                                Geometry::PropertyValueKind::Vec3,
                                PropertyTextureBakeEncoding::Normal,
                                "generated-normal");
                        RecordProgressiveTextureBakeDiagnostic(
                            scene,
                            entity,
                            GeometryPresentationSlotSemantic::Normal,
                            std::move(result.Diagnostic));
                        return true;
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
                    PropertyTextureBakeResult result =
                        ScheduleGeneratedPropertyTextureBake(
                            *options.TextureBake,
                            entity,
                            bakeOptions,
                            NormalBakePropertyName(options),
                            Geometry::PropertyValueKind::Vec3,
                            PropertyTextureBakeEncoding::Normal,
                            "generated-normal");
                    RecordProgressiveTextureBakeDiagnostic(
                        scene,
                        entity,
                        GeometryPresentationSlotSemantic::Normal,
                        std::move(result.Diagnostic));
                    if (diagnostics != nullptr &&
                        result.Succeeded())
                    {
                        ++diagnostics->ProgressiveTextureBakeJobsQueued;
                    }
                }
                else
                {
                    RecordProgressiveTextureBakeDiagnostic(
                        scene,
                        entity,
                        GeometryPresentationSlotSemantic::Normal,
                        "waiting for resolved UVs and vertex normals before property texture bake request");
                }
            }
            const bool materialHasAuthoredAlbedo =
                material != nullptr && material->BaseColorTexture.IsValid();
            const bool wantsGeneratedAlbedo =
                options.GenerateMissingAlbedoTextures &&
                !materialHasAuthoredAlbedo &&
                MeshHasVertexProperty(primitive.Mesh,
                                      options.GeneratedAlbedoPropertyName);
            if (options.TextureBake != nullptr && wantsGeneratedAlbedo &&
                hasProgressiveJobs)
            {
                JobDesc albedoBake{};
                albedoBake.DebugName = "schedule albedo GPU bake request";
                albedoBake.Scope = options.World;
                albedoBake.Kind = RuntimeTaskKinds::GeometryProcess;
                if (uvJob.IsValid())
                {
                    albedoBake.DependsOn.push_back(JobDependency{
                        .Job = uvJob,
                        .Reason = "uv atlas ready",
                    });
                }
                albedoBake.Work = [](const JobCancellation&) -> JobResultEnvelope
                {
                    return JobResultEnvelope::Make<ProgressiveEnrichmentResult>(
                        ProgressiveEnrichmentResult{
                            .Diagnostic = "property texture bake request "
                                          "dependencies ready",
                        });
                };
                albedoBake.PublishCompletion =
                    [&scene, entity,
                     textureBake = options.TextureBake,
                     requestOptions =
                         bakeOptions](KernelEventBus&,
                                     const JobResultEnvelope&) mutable -> bool
                {
                    if (!scene.IsValid(entity))
                    {
                        return false;
                    }

                    PropertyTextureBakeResult result =
                        ScheduleGeneratedPropertyTextureBake(
                            *textureBake,
                            entity,
                            requestOptions,
                            requestOptions.GeneratedAlbedoPropertyName,
                            Geometry::PropertyValueKind::Unknown,
                            PropertyTextureBakeEncoding::RgbaColor,
                            "generated-albedo");
                    RecordProgressiveTextureBakeDiagnostic(
                        scene,
                        entity,
                        GeometryPresentationSlotSemantic::Albedo,
                        std::move(result.Diagnostic));
                    return true;
                };
                (void)options.ProgressiveJobs->Submit(std::move(albedoBake));
                if (diagnostics != nullptr)
                {
                    ++diagnostics->ProgressiveTextureBakeJobsQueued;
                }
            }
            else if (options.TextureBake != nullptr &&
                wantsGeneratedAlbedo &&
                     MeshHasVertexTexcoords(primitive.Mesh))
            {
                PropertyTextureBakeResult result =
                    ScheduleGeneratedPropertyTextureBake(
                        *options.TextureBake,
                        entity,
                        bakeOptions,
                        options.GeneratedAlbedoPropertyName,
                        Geometry::PropertyValueKind::Unknown,
                        PropertyTextureBakeEncoding::RgbaColor,
                        "generated-albedo");
                RecordProgressiveTextureBakeDiagnostic(
                    scene,
                    entity,
                    GeometryPresentationSlotSemantic::Albedo,
                    std::move(result.Diagnostic));
                if (diagnostics != nullptr && result.Succeeded())
                {
                    ++diagnostics->ProgressiveTextureBakeJobsQueued;
                }
            }
        }

        Core::Expected<AssetWorkflowModelMaterializationRecord> MaterializeModelSceneAsset(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            ECS::Scene::Registry& scene,
            const Assets::AssetId modelAsset,
            const AssetWorkflowModelMaterializationOptions& options,
            AssetWorkflowModelMaterializationDiagnostics* diagnostics)
        {
            if (diagnostics != nullptr)
            {
                ++diagnostics->ModelSceneMaterializeRequests;
            }

            auto modelSpan = service.Read<Assets::AssetModelScenePayload>(modelAsset);
            if (!modelSpan.has_value())
            {
                RecordFailure(diagnostics, modelAsset, modelSpan.error());
                return Core::Err<AssetWorkflowModelMaterializationRecord>(modelSpan.error());
            }
            if (modelSpan->size() != 1u)
            {
                RecordFailure(diagnostics, modelAsset, Core::ErrorCode::AssetInvalidData);
                return Core::Err<AssetWorkflowModelMaterializationRecord>(Core::ErrorCode::AssetInvalidData);
            }

            const Assets::AssetModelScenePayload& model = (*modelSpan)[0];
            if (auto valid = Assets::ValidateAssetModelScenePayload(model); !valid.has_value())
            {
                RecordFailure(diagnostics, modelAsset, valid.error());
                return Core::Err<AssetWorkflowModelMaterializationRecord>(valid.error());
            }

            auto prepared = PreparePrimitives(
                model,
                diagnostics,
                options.ProgressiveRawGeometryFirst);
            if (!prepared.has_value())
            {
                RecordFailure(diagnostics, modelAsset, prepared.error());
                return Core::Err<AssetWorkflowModelMaterializationRecord>(prepared.error());
            }
            auto preparedNodes = PrepareNodes(model);
            if (!preparedNodes.has_value())
            {
                RecordFailure(diagnostics, modelAsset, preparedNodes.error());
                return Core::Err<AssetWorkflowModelMaterializationRecord>(preparedNodes.error());
            }
            auto preparedInstances = PreparePrimitiveInstances(
                model,
                *prepared,
                *preparedNodes);
            if (!preparedInstances.has_value())
            {
                RecordFailure(diagnostics, modelAsset, preparedInstances.error());
                return Core::Err<AssetWorkflowModelMaterializationRecord>(
                    preparedInstances.error());
            }

            std::string modelPath = model.SourcePath;
            if (auto servicePath = service.GetPath(modelAsset); servicePath.has_value())
            {
                modelPath = std::move(*servicePath);
            }

            AssetWorkflowModelMaterializationRecord state{};
            state.ModelAsset = modelAsset;

            auto embeddedTextures = LoadEmbeddedTextures(
                service,
                cache,
                model,
                modelPath,
                options,
                diagnostics);
            if (!embeddedTextures.has_value())
            {
                RecordFailure(diagnostics, modelAsset, embeddedTextures.error());
                return Core::Err<AssetWorkflowModelMaterializationRecord>(embeddedTextures.error());
            }
            state.EmbeddedTextureAssets = std::move(*embeddedTextures);

            auto& raw = scene.Raw();
            state.Nodes.reserve(model.Nodes.size());
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
                state.Nodes.push_back(AssetWorkflowModelNodeRecord{
                    .Entity = entity,
                    .NodeIndex = static_cast<std::uint32_t>(nodeIndex),
                });
                if (diagnostics != nullptr)
                {
                    ++diagnostics->NodeEntitiesCreated;
                }
            }

            state.Primitives.reserve(preparedInstances->size());
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
                // Only a generated atlas has an extent; authored UVs use the
                // bake default.
                const bool generatedAtlas =
                    primitive.TexcoordProvenance ==
                    RuntimeMeshResolvedUvProvenance::GeneratedAtlas;
                (void)PublishMeshUvAtlasExtent(
                    raw,
                    entity,
                    generatedAtlas ? primitive.UvAtlasWidth : 0u,
                    generatedAtlas ? primitive.UvAtlasHeight : 0u);
                const Assets::AssetModelMaterialPayload* material =
                    primitive.MaterialIndex < model.Materials.size()
                        ? &model.Materials[primitive.MaterialIndex]
                        : nullptr;
                if (options.ProgressiveRawGeometryFirst ||
                    options.TextureBake != nullptr)
                {
                    AttachGeometryPresentationRecipe(
                        scene,
                        entity,
                        material,
                        state.EmbeddedTextureAssets,
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

                state.Primitives.push_back(AssetWorkflowModelPrimitiveRecord{
                    .Entity = entity,
                    .NodeIndex = instance.NodeIndex,
                    .PrimitiveIndex = primitive.PrimitiveIndex,
                    .GeometryPayloadIndex = primitive.GeometryPayloadIndex,
                    .MaterialIndex = primitive.MaterialIndex,
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

    }

    struct AssetWorkflowModelMaterializer::Impl
    {
        Assets::AssetService& Service;
        Graphics::GpuAssetCache& Cache;
        ECS::Scene::Registry& Scene;
        AssetWorkflowModelMaterializationOptions Options{};
        AssetWorkflowModelMaterializationDiagnostics Diagnostics{};
        Assets::AssetEventBus::ListenerToken Token{Assets::AssetEventBus::InvalidToken};
        std::unordered_map<Assets::AssetId, AssetWorkflowModelMaterializationRecord, Assets::AssetIdHash> Records{};

        Impl(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            ECS::Scene::Registry& scene,
            AssetWorkflowModelMaterializationOptions options)
            : Service(service)
            , Cache(cache)
            , Scene(scene)
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
                DestroyEntities(Scene, record);
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
                id,
                Options,
                &Diagnostics);
            if (!state.has_value())
            {
                return Core::Err(state.error());
            }

            AssetWorkflowModelMaterializationRecord replacement = std::move(*state);
            if (auto it = Records.find(id); it != Records.end())
            {
                AssetWorkflowModelMaterializationRecord previous = std::move(it->second);
                it->second = std::move(replacement);
                DestroyEntities(Scene, previous);
            }
            else
            {
                Records.emplace(id, std::move(replacement));
            }
            return Core::Ok();
        }

        void HandleReady(const Assets::AssetId id)
        {
            ++Diagnostics.ReadyEventsObserved;

            auto model = Service.Read<Assets::AssetModelScenePayload>(id);
            if (!model.has_value())
            {
                if (!IsAssetPayloadTypeMismatch(model.error()))
                {
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

            // Reloaded only announces queued work. Replace model entities when
            // the paired Ready event supplies the complete replacement payload.
            if (event == Assets::AssetEvent::Ready)
            {
                HandleReady(id);
            }
            else if (event == Assets::AssetEvent::Destroyed)
            {
                if (auto it = Records.find(id); it != Records.end())
                {
                    DestroyEntities(Scene, it->second);
                    Records.erase(it);
                }
            }
        }
    };

    AssetWorkflowModelMaterializer::AssetWorkflowModelMaterializer(
        Assets::AssetService& service,
        Graphics::GpuAssetCache& cache,
        ECS::Scene::Registry& scene,
        AssetWorkflowModelMaterializationOptions options)
        : m_Impl(std::make_unique<Impl>(
            service,
            cache,
            scene,
            options))
    {
    }

    AssetWorkflowModelMaterializer::~AssetWorkflowModelMaterializer() = default;

    bool AssetWorkflowModelMaterializer::IsSubscribed() const noexcept
    {
        return m_Impl != nullptr
            && m_Impl->Token != Assets::AssetEventBus::InvalidToken;
    }

    AssetWorkflowModelMaterializationDiagnostics
    AssetWorkflowModelMaterializer::GetDiagnostics() const noexcept
    {
        return m_Impl != nullptr
            ? m_Impl->Diagnostics
            : AssetWorkflowModelMaterializationDiagnostics{};
    }

    const AssetWorkflowModelMaterializationRecord* AssetWorkflowModelMaterializer::FindRecord(
        const Assets::AssetId modelAsset) const noexcept
    {
        if (m_Impl == nullptr)
        {
            return nullptr;
        }
        const auto it = m_Impl->Records.find(modelAsset);
        return it != m_Impl->Records.end()
            ? &it->second
            : nullptr;
    }

    Core::Result AssetWorkflowModelMaterializer::MaterializeReadyModelScene(
        const Assets::AssetId modelAsset)
    {
        if (m_Impl == nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return m_Impl->MaterializeReadyModelScene(modelAsset);
    }

}
