module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.HtexPatchPreview.Impl;

import :Passes.HtexPatchPreview;
import :GpuColor;
import Core.Hash;
import Core.Filesystem;
import Core.Logging;
import ECS;
import Geometry;
import RHI;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    namespace
    {
        struct HalfedgeTriangle
        {
            std::array<Geometry::VertexHandle, 3> Vertices{};
            std::array<glm::vec3, 3> Positions{};
            bool Valid = false;
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

        [[nodiscard]] glm::vec4 KMeansVertexColor(const Geometry::Halfedge::Mesh& mesh,
                                                  const HalfedgeTriangle& tri,
                                                  glm::vec2 patchUV,
                                                  std::uint32_t halfedgeIndex,
                                                  std::uint32_t twinIndex) noexcept
        {
            if (!tri.Valid)
                return glm::vec4(0.0f);

            const glm::vec2 localUV = Geometry::HtexPatch::PatchToTriangleUV(halfedgeIndex, patchUV, twinIndex);
            if (!Geometry::HtexPatch::IsTriangleLocalUV(localUV))
                return glm::vec4(0.0f);

            const glm::vec3 p = EvaluateTrianglePoint(tri, localUV);
            const std::size_t winner = ClosestTriangleVertex(tri, p);
            const auto kmeansLabels = mesh.VertexProperties().Get<uint32_t>("v:kmeans_label");
            if (kmeansLabels)
            {
                return UnpackABGR(Graphics::GpuColor::LabelToColor(
                    static_cast<int>(kmeansLabels[tri.Vertices[winner].Index])));
            }

            const auto kmeansColors = mesh.VertexProperties().Get<glm::vec4>("v:kmeans_color");
            if (kmeansColors)
                return kmeansColors[tri.Vertices[winner].Index];

            return glm::vec4(0.0f);
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

        m_StagingCapacity = 0;
        m_LastPreviewHandle = {};
    }

    void HtexPatchPreviewPass::OnResize(uint32_t, uint32_t)
    {
        // Device is idle on resize in the current render-system contract.
        for (auto& img : m_PreviewImages)
            img.reset();
    }

    [[nodiscard]] glm::vec4 HtexPatchPreviewPass::DecodePackedColor(uint32_t packed) noexcept
    {
        return UnpackABGR(packed);
    }

    [[nodiscard]] glm::vec4 HtexPatchPreviewPass::TileColorFromPatch(const Geometry::Halfedge::Mesh& mesh,
                                                                     const Geometry::HtexPatch::HalfedgePatchMeta& patch) noexcept
    {
        glm::vec4 color = UnpackABGR(Graphics::GpuColor::LabelToColor(static_cast<int>(patch.EdgeIndex)));
        const auto kmeansColors = mesh.VertexProperties().Get<glm::vec4>("v:kmeans_color");
        const auto kmeansLabels = mesh.VertexProperties().Get<uint32_t>("v:kmeans_label");

        const Geometry::HalfedgeHandle h0{static_cast<Geometry::PropertyIndex>(patch.Halfedge0Index)};
        const Geometry::HalfedgeHandle h1{static_cast<Geometry::PropertyIndex>(patch.Halfedge1Index)};
        const bool hasH0 = h0.IsValid() && mesh.IsValid(h0);
        const bool hasH1 = h1.IsValid() && mesh.IsValid(h1);

        if (kmeansColors && hasH0 && hasH1)
        {
            const auto v0 = mesh.ToVertex(h0);
            const auto v1 = mesh.ToVertex(h1);
            color = 0.5f * (kmeansColors[v0.Index] + kmeansColors[v1.Index]);
        }
        else if (kmeansLabels && hasH0)
        {
            const auto v0 = mesh.ToVertex(h0);
            color = DecodePackedColor(Graphics::GpuColor::LabelToColor(static_cast<int>(kmeansLabels[v0.Index])));
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
        if (!(kmeansColors || kmeansLabels))
        {
            color.r *= 0.92f;
            color.g *= 0.96f;
            color.b *= 1.0f;
        }

        return color;
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

    [[nodiscard]] bool HtexPatchPreviewPass::BuildPreviewAtlas(const Geometry::Halfedge::Mesh& mesh,
                                                               std::vector<glm::vec4>& outPixels,
                                                               uint32_t& outWidth,
                                                               uint32_t& outHeight) const
    {
        const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
        if (patches.empty())
        {
            outWidth = 1;
            outHeight = 1;
            outPixels.assign(1, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            return false;
        }

        constexpr uint32_t kTileSize = 16u;
        constexpr uint32_t kMaxColumns = 32u;
        const uint32_t columns = std::max(1u, std::min(kMaxColumns, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(patches.size()))))));
        const uint32_t rows = static_cast<uint32_t>((patches.size() + columns - 1u) / columns);

        outWidth = columns * kTileSize;
        outHeight = rows * kTileSize;
        outPixels.assign(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        for (std::size_t i = 0; i < patches.size(); ++i)
        {
            const auto& patch = patches[i];
            const uint32_t px = static_cast<uint32_t>(i % columns) * kTileSize;
            const uint32_t py = static_cast<uint32_t>(i / columns) * kTileSize;

            glm::vec4 color = TileColorFromPatch(mesh, patch);
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
            const bool hasPerTexelKMeans = static_cast<bool>(mesh.VertexProperties().Get<uint32_t>("v:kmeans_label")) ||
                                           static_cast<bool>(mesh.VertexProperties().Get<glm::vec4>("v:kmeans_color"));

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
                            patch.Halfedge1Index);
                        const glm::vec4 tri1Color = KMeansVertexColor(
                            mesh,
                            tri1,
                            patchUV,
                            patch.Halfedge1Index,
                            patch.Halfedge0Index);

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
            return;

        std::vector<glm::vec4> pixels;
        uint32_t width = 1;
        uint32_t height = 1;
        const bool built = BuildPreviewAtlas(*meshData.MeshRef, pixels, width, height);

        m_DebugState.HasMesh = true;
        m_DebugState.LastMeshEntity = static_cast<uint32_t>(entity);
        m_DebugState.LastPatchCount = static_cast<uint32_t>(meshData.MeshRef->EdgeCount());
        m_DebugState.LastAtlasWidth = width;
        m_DebugState.LastAtlasHeight = height;
        m_DebugState.UsedKMeansColors = static_cast<bool>(meshData.MeshRef->VertexProperties().Get<glm::vec4>("v:kmeans_color")) ||
                                        static_cast<bool>(meshData.MeshRef->VertexProperties().Get<uint32_t>("v:kmeans_label"));

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
        }
        m_DebugState.PreviewImageReady = true;

        if (!built)
            return;

        if (!EnsurePerFrameBuffer<glm::vec4, FRAMES>(
                *m_Device, m_StagingBuffers, m_StagingCapacity,
                static_cast<uint32_t>(pixels.size()), 256u, "HtexPatchPreview",
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
            return;

        m_StagingBuffers[ctx.FrameIndex]->Write(pixels.data(), pixels.size() * sizeof(glm::vec4));

        const auto importName = kPreviewName;

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
                                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                                                }
                                            },
                                            [](const FinalizePassData&, const RGRegistry&, VkCommandBuffer)
                                            {
                                            });
    }
}
