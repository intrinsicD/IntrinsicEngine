module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics.Passes.HtexPatchPreview;

import Graphics.ShaderRegistry;
import Graphics.GpuColor;

import Core.Hash;
import Core.Filesystem;
import Core.Logging;

import ECS;

import Geometry.HalfedgeMesh;
import Geometry.HtexPatch;
import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;
import Geometry.Properties;

import RHI.Descriptors;
import RHI.Device;
import RHI.Image;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
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

        struct HalfedgeTriangle
        {
            std::array<Geometry::VertexHandle, 3> Vertices{};
            std::array<glm::vec3, 3> Positions{};
            bool Valid = false;
        };

        struct PreviewKMeansData
        {
            Geometry::Property<uint32_t> Labels{};
            Geometry::Property<glm::vec4> Colors{};

            [[nodiscard]] bool HasAny() const noexcept
            {
                return static_cast<bool>(Labels) || static_cast<bool>(Colors);
            }
        };

        [[nodiscard]] constexpr glm::vec4 UnpackABGR(uint32_t packed) noexcept
        {
            const float r = static_cast<float>(packed & 0xFFu) / 255.0f;
            const float g = static_cast<float>((packed >> 8) & 0xFFu) / 255.0f;
            const float b = static_cast<float>((packed >> 16) & 0xFFu) / 255.0f;
            const float a = static_cast<float>((packed >> 24) & 0xFFu) / 255.0f;
            return {r, g, b, a};
        }

        [[nodiscard]] glm::vec4 DarkenBorder(glm::vec4 color) noexcept
        {
            color.r *= 0.72f;
            color.g *= 0.72f;
            color.b *= 0.72f;
            return color;
        }

        [[nodiscard]] PreviewKMeansData GetPreviewKMeansData(const Geometry::Halfedge::Mesh& mesh) noexcept
        {
            return {
                .Labels = mesh.VertexProperties().Get<uint32_t>("v:kmeans_label"),
                .Colors = mesh.VertexProperties().Get<glm::vec4>("v:kmeans_color"),
            };
        }

        [[nodiscard]] bool HasPreviewKMeansData(const Geometry::Halfedge::Mesh& mesh) noexcept
        {
            return GetPreviewKMeansData(mesh).HasAny();
        }

        [[nodiscard]] HalfedgeTriangle BuildHalfedgeTriangle(const Geometry::Halfedge::Mesh& mesh,
                                                             Geometry::HalfedgeHandle h) noexcept
        {
            HalfedgeTriangle tri{};
            if (!h.IsValid() || !mesh.IsValid(h) || mesh.IsDeleted(h) || mesh.IsBoundary(h))
                return tri;

            const Geometry::HalfedgeHandle next = mesh.NextHalfedge(h);
            if (!next.IsValid() || !mesh.IsValid(next) || mesh.IsDeleted(next))
                return tri;

            tri.Vertices[0] = mesh.FromVertex(h);
            tri.Vertices[1] = mesh.ToVertex(h);
            tri.Vertices[2] = mesh.ToVertex(next);

            for (std::size_t i = 0; i < tri.Vertices.size(); ++i)
            {
                if (!tri.Vertices[i].IsValid() || !mesh.IsValid(tri.Vertices[i]) || mesh.IsDeleted(tri.Vertices[i]))
                    return {};

                tri.Positions[i] = mesh.Position(tri.Vertices[i]);
            }

            tri.Valid = true;
            return tri;
        }

        [[nodiscard]] glm::vec3 EvaluateTrianglePoint(const HalfedgeTriangle& tri, glm::vec2 localUV) noexcept
        {
            const float w0 = 1.0f - localUV.x - localUV.y;
            return w0 * tri.Positions[0] + localUV.x * tri.Positions[1] + localUV.y * tri.Positions[2];
        }

        [[nodiscard]] std::size_t ClosestTriangleVertex(const HalfedgeTriangle& tri, glm::vec3 p) noexcept
        {
            std::size_t best = 0;
            float bestDistance2 = glm::dot(p - tri.Positions[0], p - tri.Positions[0]);
            for (std::size_t i = 1; i < tri.Positions.size(); ++i)
            {
                const float distance2 = glm::dot(p - tri.Positions[i], p - tri.Positions[i]);
                if (distance2 < bestDistance2)
                {
                    bestDistance2 = distance2;
                    best = i;
                }
            }
            return best;
        }

        [[nodiscard]] glm::vec4 KMeansVertexColor(const Geometry::Halfedge::Mesh&,
                                                  const HalfedgeTriangle& tri,
                                                  glm::vec2 patchUV,
                                                  std::uint32_t halfedgeIndex,
                                                  std::uint32_t twinIndex,
                                                  const Geometry::Property<uint32_t>& kmeansLabels,
                                                  const Geometry::Property<glm::vec4>& kmeansColors) noexcept
        {
            if (!tri.Valid)
                return glm::vec4(0.0f);

            const glm::vec2 localUV = Geometry::HtexPatch::PatchToTriangleUV(halfedgeIndex, patchUV, twinIndex);
            if (!Geometry::HtexPatch::IsTriangleLocalUV(localUV))
                return glm::vec4(0.0f);

            const glm::vec3 p = EvaluateTrianglePoint(tri, localUV);
            const std::size_t winner = ClosestTriangleVertex(tri, p);
            if (kmeansLabels)
            {
                return UnpackABGR(Graphics::GpuColor::LabelToColor(
                    static_cast<int>(kmeansLabels[tri.Vertices[winner].Index])));
            }

            if (kmeansColors)
                return kmeansColors[tri.Vertices[winner].Index];

            return glm::vec4(0.0f);
        }

        [[nodiscard]] glm::vec4 ComputeTileColorFromPatch(const Geometry::Halfedge::Mesh& mesh,
                                                          const Geometry::HtexPatch::HalfedgePatchMeta& patch,
                                                          const PreviewKMeansData& kmeansData) noexcept
        {
            glm::vec4 color = UnpackABGR(Graphics::GpuColor::LabelToColor(static_cast<int>(patch.EdgeIndex)));

            const Geometry::HalfedgeHandle h0{static_cast<Geometry::PropertyIndex>(patch.Halfedge0Index)};
            const Geometry::HalfedgeHandle h1{static_cast<Geometry::PropertyIndex>(patch.Halfedge1Index)};
            const bool hasH0 = h0.IsValid() && mesh.IsValid(h0);
            const bool hasH1 = h1.IsValid() && mesh.IsValid(h1);

            if (kmeansData.Colors && hasH0 && hasH1)
            {
                const auto v0 = mesh.ToVertex(h0);
                const auto v1 = mesh.ToVertex(h1);
                color = 0.5f * (kmeansData.Colors[v0.Index] + kmeansData.Colors[v1.Index]);
            }
            else if (kmeansData.Labels && hasH0)
            {
                const auto v0 = mesh.ToVertex(h0);
                color = UnpackABGR(Graphics::GpuColor::LabelToColor(static_cast<int>(kmeansData.Labels[v0.Index])));
            }

            if (patch.Flags & Geometry::HtexPatch::Boundary)
            {
                color.a = 0.85f;
                color.r *= 0.88f;
                color.g *= 0.88f;
                color.b *= 0.88f;
            }
            else
            {
                color.a = 1.0f;
            }

            color = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
            if (!kmeansData.HasAny())
            {
                color.r *= 0.92f;
                color.g *= 0.96f;
                color.b *= 1.0f;
            }

            return color;
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
        if (auto selected = reg.view<ECS::Components::Selection::SelectedTag>(); !selected.empty())
        {
            for (auto entity : selected)
            {
                if (IsInterestingMeshEntity(reg, entity))
                {
                    const auto& data = reg.get<ECS::Mesh::Data>(entity);
                    if (data.MeshRef && HasPreviewKMeansData(*data.MeshRef))
                        return entity;
                }
            }

            for (auto entity : selected)
            {
                if (IsInterestingMeshEntity(reg, entity))
                    return entity;
            }
        }

        for (auto entity : reg.view<ECS::Mesh::Data>())
        {
            if (IsInterestingMeshEntity(reg, entity))
            {
                const auto& data = reg.get<ECS::Mesh::Data>(entity);
                if (data.MeshRef && HasPreviewKMeansData(*data.MeshRef))
                    return entity;
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
        std::span<const Geometry::HtexPatch::HalfedgePatchMeta> patches) noexcept
    {
        uint64_t hash = 0xcbf29ce484222325ull;
        hash = HashCombine64(hash, static_cast<uint64_t>(mesh.VertexCount()));
        hash = HashCombine64(hash, static_cast<uint64_t>(mesh.EdgeCount()));
        hash = HashCombine64(hash, static_cast<uint64_t>(mesh.FaceCount()));
        hash = HashCombine64(hash, static_cast<uint64_t>(patches.size()));

        for (std::size_t i = 0; i < mesh.VertexCount(); ++i)
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

        const PreviewKMeansData kmeansData = GetPreviewKMeansData(mesh);
        hash = HashCombine64(hash, kmeansData.Labels ? 1ull : 0ull);
        if (kmeansData.Labels)
        {
            for (std::size_t i = 0; i < mesh.VertexCount(); ++i)
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
            for (std::size_t i = 0; i < mesh.VertexCount(); ++i)
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
                                                               std::span<const Geometry::HtexPatch::HalfedgePatchMeta> patches,
                                                               std::vector<glm::vec4>& outPixels,
                                                               uint32_t& outWidth,
                                                               uint32_t& outHeight)
    {
        if (patches.empty())
        {
            outWidth = 1u;
            outHeight = 1u;
            outPixels.assign(1u, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            return false;
        }

        constexpr uint32_t kTileSize = 16u;
        constexpr uint32_t kMaxColumns = 32u;
        const uint32_t columns = std::max(1u, std::min(kMaxColumns, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(patches.size()))))));
        const uint32_t rows = static_cast<uint32_t>((patches.size() + columns - 1u) / columns);

        outWidth = columns * kTileSize;
        outHeight = rows * kTileSize;
        outPixels.assign(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const PreviewKMeansData kmeansData = GetPreviewKMeansData(mesh);
        const bool hasPerTexelKMeans = kmeansData.HasAny();

        for (std::size_t i = 0; i < patches.size(); ++i)
        {
            const auto& patch = patches[i];
            const uint32_t px = static_cast<uint32_t>(i % columns) * kTileSize;
            const uint32_t py = static_cast<uint32_t>(i / columns) * kTileSize;

            glm::vec4 color = ComputeTileColorFromPatch(mesh, patch, kmeansData);
            const float resFactor = std::clamp(static_cast<float>(patch.Resolution) / 128.0f, 0.0f, 1.0f);
            const float scale = 0.70f + 0.30f * resFactor;
            color.r *= scale;
            color.g *= scale;
            color.b *= scale;
            color = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));

            const Geometry::HalfedgeHandle h0{static_cast<Geometry::PropertyIndex>(patch.Halfedge0Index)};
            const Geometry::HalfedgeHandle h1{static_cast<Geometry::PropertyIndex>(patch.Halfedge1Index)};
            const HalfedgeTriangle tri0 = BuildHalfedgeTriangle(mesh, h0);
            const HalfedgeTriangle tri1 = BuildHalfedgeTriangle(mesh, h1);

            for (uint32_t y = 0; y < kTileSize; ++y)
            {
                for (uint32_t x = 0; x < kTileSize; ++x)
                {
                    glm::vec4 texel = color;
                    if (hasPerTexelKMeans)
                    {
                        const glm::vec2 patchUV{(static_cast<float>(x) + 0.5f) / static_cast<float>(kTileSize),
                                                (static_cast<float>(y) + 0.5f) / static_cast<float>(kTileSize)};

                        const glm::vec4 tri0Color = KMeansVertexColor(
                            mesh,
                            tri0,
                            patchUV,
                            patch.Halfedge0Index,
                            patch.Halfedge1Index,
                            kmeansData.Labels,
                            kmeansData.Colors);
                        const glm::vec4 tri1Color = KMeansVertexColor(
                            mesh,
                            tri1,
                            patchUV,
                            patch.Halfedge1Index,
                            patch.Halfedge0Index,
                            kmeansData.Labels,
                            kmeansData.Colors);

                        if (tri0Color.a > 0.0f)
                            texel = tri0Color;
                        else if (tri1Color.a > 0.0f)
                            texel = tri1Color;
                    }

                    if (x == 0u || y == 0u || x + 1u == kTileSize || y + 1u == kTileSize)
                        texel = DarkenBorder(texel);
                    else if (std::abs((static_cast<float>(x) + static_cast<float>(y) + 1.0f) -
                                      static_cast<float>(kTileSize)) <= 0.75f)
                        texel = DarkenBorder(texel);

                    const size_t dst = static_cast<size_t>(py + y) * static_cast<size_t>(outWidth) + static_cast<size_t>(px + x);
                    outPixels[dst] = texel;
                }
            }
        }

        return true;
    }

    void HtexPatchPreviewPass::AddPasses(RenderPassContext& ctx)
    {
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

        uint32_t width = 1u;
        uint32_t height = 1u;
        bool built = false;

        if (const auto patchBuild = Geometry::HtexPatch::BuildPatchMetadata(*meshData.MeshRef))
        {
            const uint64_t signature = ComputePreviewAtlasSignature(*meshData.MeshRef, patchBuild->Patches);
            if (!m_CachedAtlas.Valid || m_CachedAtlas.Signature != signature)
            {
                m_CachedAtlas.Pixels.clear();
                m_CachedAtlas.Built = BuildPreviewAtlas(
                    *meshData.MeshRef,
                    patchBuild->Patches,
                    m_CachedAtlas.Pixels,
                    m_CachedAtlas.Width,
                    m_CachedAtlas.Height);
                m_CachedAtlas.Signature = signature;
                m_CachedAtlas.Valid = true;
                ++m_CachedAtlas.Revision;
                if (m_CachedAtlas.Revision == 0)
                    m_CachedAtlas.Revision = 1;
                m_DebugState.AtlasRebuiltThisFrame = true;
            }

            width = m_CachedAtlas.Width;
            height = m_CachedAtlas.Height;
            built = m_CachedAtlas.Built;
        }
        else
        {
            m_CachedAtlas = {};
            m_CachedAtlas.Width = 1u;
            m_CachedAtlas.Height = 1u;
            m_CachedAtlas.Pixels.assign(1u, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            width = m_CachedAtlas.Width;
            height = m_CachedAtlas.Height;
        }

        m_DebugState.HasMesh = true;
        m_DebugState.LastMeshEntity = static_cast<uint32_t>(entity);
        m_DebugState.LastPatchCount = static_cast<uint32_t>(meshData.MeshRef->EdgeCount());
        m_DebugState.LastAtlasWidth = width;
        m_DebugState.LastAtlasHeight = height;
        m_DebugState.UsedKMeansColors = GetPreviewKMeansData(*meshData.MeshRef).HasAny();

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
