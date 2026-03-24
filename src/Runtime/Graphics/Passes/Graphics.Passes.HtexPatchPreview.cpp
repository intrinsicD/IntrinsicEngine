module;

#include <atomic>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"
#include "Core.Profiling.Macros.hpp"

module Graphics.Passes.HtexPatchPreview;

import Graphics.ShaderRegistry;
import Graphics.GpuColor;

import Core.Hash;
import Core.Filesystem;
import Core.Logging;
import Core.Tasks;

import ECS;

import Geometry.HalfedgeMesh;
import Geometry.HtexPatch;
import Geometry.KMeans;
import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;
import Geometry.Properties;

import RHI.Buffer;
import RHI.CommandUtils;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    struct HtexPatchPreviewPass::PendingPreviewBake
    {
        std::atomic<bool> Ready{false};
        uint64_t Signature = 0;
        uint64_t Revision = 0;
        Geometry::Halfedge::Mesh MeshCopy{};
        std::vector<Geometry::HtexPatch::HalfedgePatchMeta> Patches{};
        CachedPreviewAtlas Atlas{};
    };

    namespace
    {
        [[nodiscard]] constexpr uint64_t HashCombine64(uint64_t seed, uint64_t value) noexcept
        {
            value *= 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
            value ^= (value >> 31u);
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
            return seed;
        }

        [[nodiscard]] constexpr uint64_t HashFloat(float value) noexcept
        {
            return std::bit_cast<uint32_t>(value);
        }

        [[nodiscard]] uint64_t HashVec3(uint64_t seed, const glm::vec3& value) noexcept
        {
            seed = HashCombine64(seed, HashFloat(value.x));
            seed = HashCombine64(seed, HashFloat(value.y));
            seed = HashCombine64(seed, HashFloat(value.z));
            return seed;
        }

        [[nodiscard]] uint64_t HashVec4(uint64_t seed, const glm::vec4& value) noexcept
        {
            seed = HashCombine64(seed, HashFloat(value.r));
            seed = HashCombine64(seed, HashFloat(value.g));
            seed = HashCombine64(seed, HashFloat(value.b));
            seed = HashCombine64(seed, HashFloat(value.a));
            return seed;
        }

        [[nodiscard]] float VertexLabelScalar(
            const Geometry::Halfedge::Mesh& mesh,
            const Geometry::Property<uint32_t>& labels,
            Geometry::VertexHandle v) noexcept
        {
            if (!v.IsValid() || !mesh.IsValid(v) || mesh.IsDeleted(v) || !labels)
                return -1.0f;
            return static_cast<float>(labels[v.Index]);
        }

        [[nodiscard]] std::vector<glm::vec3> ReconstructPreviewCentroids(
            const Geometry::Halfedge::Mesh& mesh,
            const Geometry::Property<uint32_t>& labels)
        {
            PROFILE_SCOPE("HtexPatchPreview::ReconstructPreviewCentroids");

            if (!labels)
                return {};

            uint32_t clusterCount = 0u;
            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                    continue;
                clusterCount = std::max(clusterCount, labels[v.Index] + 1u);
            }

            if (clusterCount == 0u)
                return {};

            std::vector<glm::vec3> points;
            std::vector<uint32_t> pointLabels;
            points.reserve(mesh.VertexCount());
            pointLabels.reserve(mesh.VertexCount());

            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                    continue;

                const glm::vec3 p = mesh.Position(v);
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    continue;

                points.push_back(p);
                pointLabels.push_back(labels[v.Index]);
            }

            return Geometry::KMeans::RecomputeCentroids(points, pointLabels, clusterCount);
        }

        [[nodiscard]] float FirstPatchVertexLabel(const Geometry::Halfedge::Mesh& mesh,
                                                  const Geometry::HtexPatch::HalfedgePatchMeta& patch,
                                                  const Geometry::Property<uint32_t>& kmeansLabels) noexcept
        {
            const auto sampleHalfedgeLabel = [&](std::uint32_t halfedgeIndex) noexcept -> float
            {
                const auto tri = Geometry::HtexPatch::BuildHalfedgeTriangle(mesh, halfedgeIndex);
                if (!tri)
                    return -1.0f;

                for (Geometry::VertexHandle v : tri->Vertices)
                {
                    const float label = VertexLabelScalar(mesh, kmeansLabels, v);
                    if (label >= 0.0f)
                        return label;
                }

                return -1.0f;
            };

            if (const float label0 = sampleHalfedgeLabel(patch.Halfedge0Index); label0 >= 0.0f)
                return label0;
            return sampleHalfedgeLabel(patch.Halfedge1Index);
        }

        [[nodiscard]] float KMeansVertexScalar(const Geometry::Halfedge::Mesh& mesh,
                                               const Geometry::HtexPatch::HalfedgePatchMeta& patch,
                                               glm::vec2 patchUV,
                                               std::span<const glm::vec3> kmeansCentroids,
                                               const Geometry::Property<uint32_t>& kmeansLabels) noexcept
        {
            if (const auto winner = Geometry::HtexPatch::ClassifyPatchSurfacePointToCentroid(
                    mesh, patch, patchUV, kmeansCentroids))
            {
                return static_cast<float>(*winner);
            }

            return FirstPatchVertexLabel(mesh, patch, kmeansLabels);
        }

        [[nodiscard]] float ComputeTileScalarFromPatch(const Geometry::Halfedge::Mesh& mesh,
                                                       const Geometry::HtexPatch::HalfedgePatchMeta& patch,
                                                       const PreviewKMeansData& kmeansData) noexcept
        {
            if (kmeansData.HasCentroidField())
            {
                if (const float centerScalar = KMeansVertexScalar(
                        mesh,
                        patch,
                        glm::vec2{0.5f, 0.5f},
                        kmeansData.Centroids,
                        kmeansData.Labels);
                    centerScalar >= 0.0f)
                {
                    return centerScalar;
                }
            }

            return static_cast<float>(patch.EdgeIndex);
        }

    }

    void HtexPatchPreviewPass::Initialize(RHI::VulkanDevice& device,
                                          RHI::DescriptorAllocator&,
                                          RHI::DescriptorLayout&)
    {
        m_Device = &device;
        m_DebugState = {};
        m_DebugState.Initialized = true;
    }

    void HtexPatchPreviewPass::Shutdown()
    {
        if (!m_Device)
            return;

        for (auto& img : m_PreviewImages)
            img.reset();
        for (auto& buf : m_StagingBuffers)
            buf.reset();

        m_UploadedAtlasRevision.fill(0);
        m_CachedAtlas = {};
        m_CachedInputs = {};
        m_CachedPreviewSignature = 0;
        m_CachedPatchBuild = {};
        m_PendingBake.reset();
        m_StagingCapacity = 0;
    }

    void HtexPatchPreviewPass::OnResize(uint32_t, uint32_t)
    {
        // Device is idle on resize in the current render-system contract.
        for (auto& img : m_PreviewImages)
            img.reset();
        m_UploadedAtlasRevision.fill(0);
    }


    [[nodiscard]] bool HtexPatchPreviewPass::IsInterestingMeshEntity(const entt::registry& reg, entt::entity entity)
    {
        if (!reg.valid(entity) || !reg.all_of<ECS::Mesh::Data>(entity))
            return false;
        const auto& data = reg.get<ECS::Mesh::Data>(entity);
        return static_cast<bool>(data.MeshRef) && data.MeshRef->EdgeCount() > 0;
    }

    [[nodiscard]] std::optional<entt::entity> HtexPatchPreviewPass::FindSourceMeshEntity(const entt::registry& reg)
    {
        std::optional<entt::entity> freshestKMeansMesh;
        uint64_t freshestRevision = 0;
        auto considerFreshest = [&](entt::entity entity)
        {
            if (!IsInterestingMeshEntity(reg, entity))
                return;

            const auto& data = reg.get<ECS::Mesh::Data>(entity);
            if (!data.MeshRef)
                return;

            const auto labels = data.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
            const auto colors = data.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
            if (!(labels || colors))
                return;

            if (!freshestKMeansMesh || data.KMeansResultRevision >= freshestRevision)
            {
                freshestKMeansMesh = entity;
                freshestRevision = data.KMeansResultRevision;
            }
        };

        if (auto selected = reg.view<ECS::Components::Selection::SelectedTag>(); !selected.empty())
        {
            for (auto entity : selected)
            {
                if (IsInterestingMeshEntity(reg, entity))
                {
                    const auto& data = reg.get<ECS::Mesh::Data>(entity);
                    if (data.MeshRef)
                    {
                        const auto labels = data.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
                        const auto colors = data.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
                        if (labels || colors)
                            return entity;
                    }
                }
            }

            for (auto entity : selected)
            {
                if (IsInterestingMeshEntity(reg, entity))
                    return entity;
            }
        }

        for (auto entity : reg.view<ECS::Mesh::Data>())
            considerFreshest(entity);
        if (freshestKMeansMesh)
            return freshestKMeansMesh;

        for (auto entity : reg.view<ECS::Mesh::Data>())
        {
            if (IsInterestingMeshEntity(reg, entity))
            {
                const auto& data = reg.get<ECS::Mesh::Data>(entity);
                if (data.MeshRef)
                {
                    const auto labels = data.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
                    const auto colors = data.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
                    if (labels || colors)
                        return entity;
                }
            }
        }

        for (auto entity : reg.view<ECS::Mesh::Data>())
        {
            if (IsInterestingMeshEntity(reg, entity))
                return entity;
        }

        return std::nullopt;
    }

    [[nodiscard]] uint64_t HtexPatchPreviewPass::ComputePreviewAtlasSignature(
        const Geometry::Halfedge::Mesh& mesh,
        const PreviewKMeansData& kmeansData,
        std::span<const Geometry::HtexPatch::HalfedgePatchMeta> patches) noexcept
    {
        PROFILE_SCOPE("HtexPatchPreview::ComputePreviewAtlasSignature");

        uint64_t hash = 0xcbf29ce484222325ull;
        hash = HashCombine64(hash, static_cast<uint64_t>(mesh.VertexCount()));
        hash = HashCombine64(hash, static_cast<uint64_t>(mesh.EdgeCount()));
        hash = HashCombine64(hash, static_cast<uint64_t>(mesh.FaceCount()));
        hash = HashCombine64(hash, static_cast<uint64_t>(patches.size()));

        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                continue;
            hash = HashVec3(hash, mesh.Position(v));
        }

        for (const auto& patch : patches)
        {
            hash = HashCombine64(hash, patch.EdgeIndex);
            hash = HashCombine64(hash, patch.Halfedge0Index);
            hash = HashCombine64(hash, patch.Halfedge1Index);
            hash = HashCombine64(hash, patch.Face0Index);
            hash = HashCombine64(hash, patch.Face1Index);
            hash = HashCombine64(hash, patch.Resolution);
            hash = HashCombine64(hash, patch.LayerIndex);
            hash = HashCombine64(hash, patch.Flags);
        }

        hash = HashCombine64(hash, kmeansData.Labels ? 1ull : 0ull);
        if (kmeansData.Labels)
        {
            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                    continue;
                hash = HashCombine64(hash, static_cast<uint64_t>(kmeansData.Labels[v.Index]));
            }
        }

        hash = HashCombine64(hash, kmeansData.Colors ? 1ull : 0ull);
        if (kmeansData.Colors)
        {
            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                    continue;
                hash = HashVec4(hash, kmeansData.Colors[v.Index]);
            }
        }

        return hash;
    }

    [[nodiscard]] bool HtexPatchPreviewPass::BuildPreviewAtlas(const Geometry::Halfedge::Mesh& mesh,
                                                               const PreviewKMeansData& kmeansData,
                                                               std::span<const Geometry::HtexPatch::HalfedgePatchMeta> patches,
                                                               std::vector<glm::vec4>& outPixels,
                                                               uint32_t& outWidth,
                                                               uint32_t& outHeight)
    {
        PROFILE_SCOPE("HtexPatchPreview::BuildPreviewAtlas");

        if (patches.empty())
        {
            outWidth = 1u;
            outHeight = 1u;
            outPixels.assign(1u, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            return false;
        }

        constexpr uint32_t kTileSize = 16u;
        constexpr uint32_t kMaxColumns = 32u;
        const uint32_t columns = std::max(1u,
                                          std::min(kMaxColumns,
                                                   static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(patches.size()))))));
        const uint32_t rows = static_cast<uint32_t>((patches.size() + columns - 1u) / columns);

        outWidth = columns * kTileSize;
        outHeight = rows * kTileSize;
        outPixels.assign(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        const bool hasPerTexelKMeans = kmeansData.HasCentroidField();
        Geometry::HtexPatch::PatchAtlasLayout categoricalLayout{};
        std::vector<std::uint32_t> categoricalAtlasTexels;
        const bool hasCategoricalAtlas = !hasPerTexelKMeans &&
                                         Geometry::HtexPatch::BuildCategoricalPatchAtlas(mesh,
                                                                                         patches,
                                                                                         kmeansData.Centroids,
                                                                                         categoricalAtlasTexels,
                                                                                         categoricalLayout,
                                                                                         Geometry::HtexPatch::kInvalidIndex);

        for (std::size_t i = 0; i < patches.size(); ++i)
        {
            const auto& patch = patches[i];
            const uint32_t px = static_cast<uint32_t>(i % columns) * kTileSize;
            const uint32_t py = static_cast<uint32_t>(i / columns) * kTileSize;

            const float scalar = ComputeTileScalarFromPatch(mesh, patch, kmeansData);
            for (uint32_t y = 0; y < kTileSize; ++y)
            {
                for (uint32_t x = 0; x < kTileSize; ++x)
                {
                    float texelScalar = scalar;
                    if (hasPerTexelKMeans)
                    {
                        const glm::vec2 patchUV{(static_cast<float>(x) + 0.5f) / static_cast<float>(kTileSize),
                                                (static_cast<float>(y) + 0.5f) / static_cast<float>(kTileSize)};

                        const float sampledScalar = KMeansVertexScalar(
                            mesh,
                            patch,
                            patchUV,
                            kmeansData.Centroids,
                            kmeansData.Labels);
                        if (sampledScalar >= 0.0f)
                            texelScalar = sampledScalar;
                    }
                    else if (hasCategoricalAtlas && categoricalLayout.Width == outWidth && categoricalLayout.Height == outHeight)
                    {
                        const size_t src = static_cast<size_t>(py + y) * static_cast<size_t>(categoricalLayout.Width) +
                                           static_cast<size_t>(px + x);
                        if (src < categoricalAtlasTexels.size() &&
                            categoricalAtlasTexels[src] != Geometry::HtexPatch::kInvalidIndex)
                        {
                            texelScalar = static_cast<float>(categoricalAtlasTexels[src]);
                        }
                    }

                    const glm::vec4 texel{texelScalar, 0.0f, 0.0f, 1.0f};
                    const size_t dst = static_cast<size_t>(py + y) * static_cast<size_t>(outWidth) +
                                       static_cast<size_t>(px + x);
                    outPixels[dst] = texel;
                }
            }
        }

        return true;
    }

    void HtexPatchPreviewPass::AddPasses(RenderPassContext& ctx)
    {
        PROFILE_SCOPE("HtexPatchPreview::AddPasses");

        if (!m_Device || ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
            return;

        m_DebugState.AtlasRebuiltThisFrame = false;
        m_DebugState.AtlasUploadQueuedThisFrame = false;

        auto& reg = ctx.Scene.GetRegistry();
        const auto source = FindSourceMeshEntity(reg);
        if (!source)
        {
            m_DebugState.HasMesh = false;
            m_DebugState.PreviewImageReady = false;
            return;
        }

        const auto entity = *source;
        const auto& meshData = reg.get<ECS::Mesh::Data>(entity);
        if (!meshData.MeshRef)
        {
            m_DebugState.HasMesh = false;
            m_DebugState.PreviewImageReady = false;
            return;
        }

        if (m_PendingBake && m_PendingBake->Ready.load(std::memory_order_acquire))
        {
            m_CachedAtlas = std::move(m_PendingBake->Atlas);
            m_PendingBake.reset();
            m_DebugState.AtlasRebuiltThisFrame = true;
        }

        const PreviewKMeansData kmeansData{
            .Labels = meshData.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label"),
            .Colors = meshData.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color"),
        };

        const bool cachedInputsMatch = m_CachedInputs.Valid &&
                                       m_CachedInputs.SourceEntity == entity &&
                                       m_CachedInputs.Mesh == meshData.MeshRef.get() &&
                                       m_CachedInputs.VertexCount == meshData.MeshRef->VertexCount() &&
                                       m_CachedInputs.EdgeCount == meshData.MeshRef->EdgeCount() &&
                                       m_CachedInputs.FaceCount == meshData.MeshRef->FaceCount() &&
                                       m_CachedInputs.KMeansResultRevision == meshData.KMeansResultRevision &&
                                       m_CachedInputs.HasLabels == static_cast<bool>(kmeansData.Labels) &&
                                       m_CachedInputs.HasColors == static_cast<bool>(kmeansData.Colors);

        uint64_t desiredSignature = m_CachedPreviewSignature;
        if (!cachedInputsMatch)
        {
            PROFILE_SCOPE("HtexPatchPreview::RefreshPatchCache");

            if (const auto patchBuild = Geometry::HtexPatch::BuildPatchMetadata(*meshData.MeshRef))
            {
                m_CachedPatchBuild = *patchBuild;
                m_CachedInputs = CachedPreviewInputs{
                    .SourceEntity = entity,
                    .Mesh = meshData.MeshRef.get(),
                    .VertexCount = meshData.MeshRef->VertexCount(),
                    .EdgeCount = meshData.MeshRef->EdgeCount(),
                    .FaceCount = meshData.MeshRef->FaceCount(),
                    .KMeansResultRevision = meshData.KMeansResultRevision,
                    .HasLabels = static_cast<bool>(kmeansData.Labels),
                    .HasColors = static_cast<bool>(kmeansData.Colors),
                    .Valid = true,
                };

                desiredSignature = ComputePreviewAtlasSignature(*meshData.MeshRef, kmeansData, m_CachedPatchBuild.Patches);
                m_CachedPreviewSignature = desiredSignature;
            }
            else
            {
                m_CachedInputs = {};
                m_CachedPatchBuild = {};
                m_CachedPreviewSignature = 0;
                m_CachedAtlas = {};
            }
        }
        else if (desiredSignature == 0u)
        {
            desiredSignature = ComputePreviewAtlasSignature(*meshData.MeshRef, kmeansData, m_CachedPatchBuild.Patches);
            m_CachedPreviewSignature = desiredSignature;
        }

        uint32_t width = 1u;
        uint32_t height = 1u;
        bool built = false;

        const bool atlasReady = desiredSignature != 0u &&
                                m_CachedAtlas.Valid &&
                                m_CachedAtlas.Built &&
                                m_CachedAtlas.Signature == desiredSignature;

        if (!atlasReady)
        {
            if (desiredSignature != 0u)
            {
                if (!m_PendingBake || m_PendingBake->Signature != desiredSignature)
                {
                    PROFILE_SCOPE("HtexPatchPreview::EnqueueBake");

                    auto pending = std::make_shared<PendingPreviewBake>();
                    pending->Signature = desiredSignature;
                    pending->Revision = m_CachedAtlas.Revision != 0u ? (m_CachedAtlas.Revision + 1u) : 1u;
                    pending->MeshCopy = Geometry::Halfedge::Mesh{*meshData.MeshRef};
                    pending->Patches = m_CachedPatchBuild.Patches;
                    m_PendingBake = pending;

                    auto bakeJob = [pending]() mutable
                    {
                        PROFILE_SCOPE("HtexPatchPreview::BakeJob");

                        auto& meshCopy = pending->MeshCopy;
                        auto& patchesCopy = pending->Patches;

                        PreviewKMeansData workerKMeans{
                            .Labels = meshCopy.VertexProperties().Get<uint32_t>("v:kmeans_label"),
                            .Colors = meshCopy.VertexProperties().Get<glm::vec4>("v:kmeans_color"),
                        };

                        std::vector<glm::vec3> centroids = ReconstructPreviewCentroids(meshCopy, workerKMeans.Labels);
                        workerKMeans.Centroids = std::span<const glm::vec3>{centroids};

                        CachedPreviewAtlas atlas{};
                        atlas.Signature = pending->Signature;
                        atlas.Revision = pending->Revision;
                        atlas.Built = BuildPreviewAtlas(meshCopy,
                                                        workerKMeans,
                                                        patchesCopy,
                                                        atlas.Pixels,
                                                        atlas.Width,
                                                        atlas.Height);
                        atlas.Valid = atlas.Built;
                        pending->Atlas = std::move(atlas);
                        pending->Ready.store(true, std::memory_order_release);
                    };

                    if (Core::Tasks::Scheduler::IsInitialized())
                        Core::Tasks::Scheduler::Dispatch(std::move(bakeJob));
                    else
                        bakeJob();
                }

                m_DebugState.HasMesh = true;
                m_DebugState.LastMeshEntity = static_cast<uint32_t>(entity);
                m_DebugState.LastPatchCount = static_cast<uint32_t>(m_CachedPatchBuild.Patches.size());
                m_DebugState.LastAtlasWidth = 0u;
                m_DebugState.LastAtlasHeight = 0u;
                m_DebugState.UsedKMeansColors = [&]() noexcept
                {
                    const auto labels = meshData.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
                    const auto colors = meshData.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
                    return static_cast<bool>(labels) || static_cast<bool>(colors);
                }();
                m_DebugState.PreviewImageReady = false;
                return;
            }
        }

        if (atlasReady)
        {
            width = m_CachedAtlas.Width;
            height = m_CachedAtlas.Height;
            built = true;
        }

        m_DebugState.HasMesh = true;
        m_DebugState.LastMeshEntity = static_cast<uint32_t>(entity);
        m_DebugState.LastPatchCount = static_cast<uint32_t>(m_CachedPatchBuild.Patches.size());
        m_DebugState.LastAtlasWidth = width;
        m_DebugState.LastAtlasHeight = height;
        m_DebugState.UsedKMeansColors = [&]() noexcept
        {
            const auto labels = meshData.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label");
            const auto colors = meshData.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color");
            return static_cast<bool>(labels) || static_cast<bool>(colors);
        }();

        if (ctx.FrameIndex >= FRAMES)
            return;

        if (!m_PreviewImages[ctx.FrameIndex] ||
            m_PreviewImages[ctx.FrameIndex]->GetWidth() != width ||
            m_PreviewImages[ctx.FrameIndex]->GetHeight() != height)
        {
            m_PreviewImages[ctx.FrameIndex] = std::make_unique<RHI::VulkanImage>(
                *m_Device,
                width,
                height,
                1,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            m_UploadedAtlasRevision[ctx.FrameIndex] = 0;
        }
        m_DebugState.PreviewImageReady = true;

        if (!built)
            return;

        const auto importName = kPreviewName;
        const bool needsUpload = (m_CachedAtlas.Revision != 0u) &&
                                 (m_UploadedAtlasRevision[ctx.FrameIndex] != m_CachedAtlas.Revision);

        if (!needsUpload)
        {
            ctx.Graph.AddPass<FinalizePassData>("HtexPatchPreview.Bind",
                                                [&, importName](FinalizePassData& data, RGBuilder& builder)
                                                {
                                                    data.Target = builder.ImportTexture(
                                                        importName,
                                                        m_PreviewImages[ctx.FrameIndex]->GetHandle(),
                                                        m_PreviewImages[ctx.FrameIndex]->GetView(),
                                                        m_PreviewImages[ctx.FrameIndex]->GetFormat(),
                                                        {width, height},
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                                    if (data.Target.IsValid())
                                                    {
                                                        data.Target = builder.Read(
                                                            data.Target,
                                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                                                        ctx.Blackboard.Add(importName, data.Target);
                                                    }
                                                },
                                                [](const FinalizePassData&, const RGRegistry&, VkCommandBuffer)
                                                {
                                                });
            return;
        }

        if (!EnsurePerFrameBuffer<glm::vec4, FRAMES>(
                *m_Device, m_StagingBuffers, m_StagingCapacity,
                static_cast<uint32_t>(m_CachedAtlas.Pixels.size()), 256u, "HtexPatchPreview",
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
            return;

        m_StagingBuffers[ctx.FrameIndex]->Write(
            m_CachedAtlas.Pixels.data(),
            m_CachedAtlas.Pixels.size() * sizeof(glm::vec4));
        m_DebugState.AtlasUploadQueuedThisFrame = true;

        ctx.Graph.AddPass<UploadPassData>("HtexPatchPreview.Upload",
                                          [&, width, height, importName](UploadPassData& data, RGBuilder& builder)
                                          {
                                              data.Target = builder.ImportTexture(
                                                  importName,
                                                  m_PreviewImages[ctx.FrameIndex]->GetHandle(),
                                                  m_PreviewImages[ctx.FrameIndex]->GetView(),
                                                  m_PreviewImages[ctx.FrameIndex]->GetFormat(),
                                                  {width, height},
                                                  VK_IMAGE_LAYOUT_UNDEFINED);
                                              if (data.Target.IsValid())
                                              {
                                                  data.Target = builder.Write(
                                                      data.Target,
                                                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
                                                  ctx.Blackboard.Add(importName, data.Target);
                                              }
                                          },
                                          [this, frameIndex = ctx.FrameIndex, width, height](const UploadPassData& data,
                                                                                           const RGRegistry& reg,
                                                                                           VkCommandBuffer cmd)
                                          {
                                              if (!data.Target.IsValid() || frameIndex >= FRAMES)
                                                  return;

                                              const VkImage dstImage = reg.GetImage(data.Target);
                                              const VkBuffer srcBuffer = m_StagingBuffers[frameIndex]->GetHandle();
                                              if (dstImage == VK_NULL_HANDLE || srcBuffer == VK_NULL_HANDLE)
                                                  return;

                                              VkBufferImageCopy region{};
                                              region.bufferOffset = 0;
                                              region.bufferRowLength = 0;
                                              region.bufferImageHeight = 0;
                                              region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                              region.imageSubresource.mipLevel = 0;
                                              region.imageSubresource.baseArrayLayer = 0;
                                              region.imageSubresource.layerCount = 1;
                                              region.imageExtent = {width, height, 1};

                                              vkCmdCopyBufferToImage(cmd,
                                                                     srcBuffer,
                                                                     dstImage,
                                                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                     1,
                                                                     &region);
                                          });

        ctx.Graph.AddPass<FinalizePassData>("HtexPatchPreview.Finalize",
                                            [&, importName](FinalizePassData& data, RGBuilder& builder)
                                            {
                                                const auto handle = ctx.Blackboard.Get(importName);
                                                if (handle.IsValid())
                                                {
                                                    data.Target = builder.Read(
                                                        handle,
                                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                                                }
                                            },
                                            [this, frameIndex = ctx.FrameIndex](const FinalizePassData&, const RGRegistry&, VkCommandBuffer)
                                            {
                                                if (frameIndex < FRAMES)
                                                    m_UploadedAtlasRevision[frameIndex] = m_CachedAtlas.Revision;
                                            });
    }
}
