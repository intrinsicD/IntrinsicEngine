module;
#include <RHI/RHI.Vulkan.hpp>
#include <vector>
#include <algorithm>

module Runtime.RenderGraph;
import Core.Logging;

namespace Runtime::Graph
{
    // --- RGRegistry ---
    VkImage RGRegistry::GetImage(RGResourceHandle handle) const
    {
        if (handle.ID >= m_PhysicalImages.size()) return VK_NULL_HANDLE;
        return m_PhysicalImages[handle.ID].Image;
    }

    VkImageView RGRegistry::GetImageView(RGResourceHandle handle) const
    {
        if (handle.ID >= m_PhysicalImages.size()) return VK_NULL_HANDLE;
        return m_PhysicalImages[handle.ID].View;
    }

    void RGRegistry::RegisterImage(ResourceID id, VkImage img, VkImageView view)
    {
        if (m_PhysicalImages.size() <= id) m_PhysicalImages.resize(id + 1);
        m_PhysicalImages[id] = {img, view};
    }

    // --- RGBuilder ---
    RGResourceHandle RGBuilder::Read(RGResourceHandle resource)
    {
        m_Graph.m_Passes[m_PassIndex].Reads.push_back(resource.ID);
        return resource;
    }

    RGResourceHandle RGBuilder::Write(RGResourceHandle resource)
    {
        m_Graph.m_Passes[m_PassIndex].Writes.push_back(resource.ID);
        return resource;
    }

    RGResourceHandle RGBuilder::ImportTexture(const std::string& name, VkImage image, VkImageView view, VkFormat,
                                              VkExtent2D extent)
    {
        ResourceID id = m_Graph.CreateResourceInternal(name, ResourceType::Import);
        auto& node = m_Graph.m_Resources[id];
        node.PhysicalImage = image;
        node.PhysicalView = view;
        node.InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Or PRESENT_SRC if coming from swapchain
        node.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        node.Extent = extent;

        m_Graph.m_Registry.RegisterImage(id, image, view);
        return {id};
    }

    VkExtent2D RGBuilder::GetTextureExtent(RGResourceHandle handle) const
    {
        if (handle.ID < m_Graph.m_Resources.size())
        {
            return m_Graph.m_Resources[handle.ID].Extent;
        }
        return {0, 0};
    }

    // --- RenderGraph ---
    RenderGraph::RenderGraph(RHI::VulkanDevice& device) : m_Device(device)
    {
    }

    RenderGraph::~RenderGraph()
    {
    }

    RGPass& RenderGraph::CreatePassInternal(const std::string& name)
    {
        return m_Passes.emplace_back(RGPass{name});
    }

    ResourceID RenderGraph::CreateResourceInternal(const std::string& name, ResourceType type)
    {
        ResourceID id = (ResourceID)m_Resources.size();
        ResourceNode node{};
        node.Name = name;
        node.Type = type;
        m_Resources.push_back(node);
        return id;
    }

    void RenderGraph::Reset()
    {
        m_Passes.clear();
        m_Resources.clear();
        m_Barriers.clear();
        // In a real engine, we'd recycle registry/arena memory here
    }

    void RenderGraph::Compile()
    {
        m_Barriers.resize(m_Passes.size());

        // Simple linear sweep for automatic barrier insertion
        for (size_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx)
        {
            const auto& pass = m_Passes[passIdx];

            // 1. Check Writes (Transitions to Attachments)
            for (ResourceID id : pass.Writes)
            {
                auto& res = m_Resources[id];
                VkImageLayout oldLayout = res.CurrentLayout;
                VkImageLayout newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                // Simple heuristic: if it's depth format, use depth layout
                // (Real implementation needs format check)

                if (oldLayout != newLayout)
                {
                    VkImageMemoryBarrier2 barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.image = res.PhysicalImage;
                    barrier.oldLayout = oldLayout;
                    barrier.newLayout = newLayout;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // Naive for now
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; // Todo: Fix aspect

                    m_Barriers[passIdx].ImageBarriers.push_back(barrier);
                    res.CurrentLayout = newLayout;
                }
            }

            // 2. Check Reads (Transitions to Shader Read)
            for (ResourceID id : pass.Reads)
            {
                auto& res = m_Resources[id];
                VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                if (res.CurrentLayout != newLayout)
                {
                    VkImageMemoryBarrier2 barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.image = res.PhysicalImage;
                    barrier.oldLayout = res.CurrentLayout;
                    barrier.newLayout = newLayout;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                    m_Barriers[passIdx].ImageBarriers.push_back(barrier);
                    res.CurrentLayout = newLayout;
                }
            }
        }
    }

    void RenderGraph::Execute(VkCommandBuffer cmd)
    {
        for (size_t i = 0; i < m_Passes.size(); ++i)
        {
            // 1. Submit Barriers
            if (!m_Barriers[i].ImageBarriers.empty())
            {
                VkDependencyInfo depInfo{};
                depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.imageMemoryBarrierCount = (uint32_t)m_Barriers[i].ImageBarriers.size();
                depInfo.pImageMemoryBarriers = m_Barriers[i].ImageBarriers.data();
                vkCmdPipelineBarrier2(cmd, &depInfo);
            }

            // 2. Execute Pass
            // Core::Log::Debug("Executing Pass: {}", m_Passes[i].Name);
            m_Passes[i].Execute(m_Registry, cmd);
        }
    }
}
