#include <cstring>
#include <vector>
#include <chrono> // For animation timing

// GLM for 3D Math
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Vulkan depth range [0,1]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "RHI/RHI.Vulkan.hpp" // Should define VK_NO_PROTOTYPES etc.

import Core.Logging;
import Core.Window;
import Runtime.RHI.Context;
import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Renderer;
import Runtime.RHI.Shader;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Buffer;
import Runtime.RHI.CommandUtils;
import Runtime.RHI.Types;
import Runtime.RHI.Descriptors; // Import the new module

using namespace Core;

const std::vector<Runtime::RHI::Vertex> vertices = {
    // --- Triangle 1: RGB (Foreground, Z = 0.0) ---
    {{ 0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // Top Red
    {{ 0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // Bottom Right Green
    {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // Bottom Left Blue

    // --- Triangle 2: White (Background, Z = -0.5, Shifted Right) ---
    // Note: We repeat vertices because we aren't using an Index Buffer yet.
    {{ 0.2f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}}, // Top
    {{ 0.7f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}}, // Bottom Right
    {{-0.3f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}}  // Bottom Left
};

int main()
{
    Log::Info("Intrinsic Engine Starting...");

    Windowing::WindowProps props;
    props.Title = "Intrinsic Research Renderer";
    props.Width = 1600;
    props.Height = 900;
    Windowing::Window window(props);

    Runtime::RHI::ContextConfig rhiConfig;
    rhiConfig.EnableValidation = true;
    Runtime::RHI::VulkanContext context(rhiConfig, window);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!window.CreateSurface(context.GetInstance(), nullptr, &surface))
    {
        return -1;
    }

    {
        Runtime::RHI::VulkanDevice device(context, surface);
        Runtime::RHI::VulkanSwapchain swapchain(device, window);
        Runtime::RHI::SimpleRenderer renderer(device, swapchain);

        // ---------------------------------------------------------
        // 1. Vertex Buffer Setup
        // ---------------------------------------------------------
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        Runtime::RHI::VulkanBuffer stagingBuffer(
            device,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY
        );

        void* data = stagingBuffer.Map();
        memcpy(data, vertices.data(), (size_t)bufferSize);
        stagingBuffer.Unmap();

        Runtime::RHI::VulkanBuffer vertexBuffer(
            device,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        Runtime::RHI::CommandUtils::ExecuteImmediate(device, [&](VkCommandBuffer cmd)
        {
            VkBufferCopy copyRegion{};
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer.GetHandle(), vertexBuffer.GetHandle(), 1, &copyRegion);
        });

        // ---------------------------------------------------------
        // 2. Uniform Buffer & Descriptor Setup
        // ---------------------------------------------------------
        Runtime::RHI::VulkanBuffer uniformBuffer(
            device,
            sizeof(Runtime::RHI::UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU // CPU writes, GPU reads
        );

        Runtime::RHI::DescriptorLayout descriptorLayout(device);
        Runtime::RHI::DescriptorPool descriptorPool(device);

        VkDescriptorSet descriptorSet = descriptorPool.Allocate(descriptorLayout.GetHandle());

        // LINK Buffer -> Descriptor Set
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer.GetHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(Runtime::RHI::UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device.GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);

        // ---------------------------------------------------------
        // 3. Pipeline Setup
        // ---------------------------------------------------------
        Runtime::RHI::ShaderModule vertShader(device, "shaders/triangle.vert.spv", Runtime::RHI::ShaderStage::Vertex);
        Runtime::RHI::ShaderModule fragShader(device, "shaders/triangle.frag.spv", Runtime::RHI::ShaderStage::Fragment);

        Runtime::RHI::PipelineConfig pipeConfig;
        pipeConfig.VertexShader = &vertShader;
        pipeConfig.FragmentShader = &fragShader;

        // Pass the Layout Handle here!
        Runtime::RHI::GraphicsPipeline pipeline(device, swapchain, pipeConfig, descriptorLayout.GetHandle());

        // ---------------------------------------------------------
        // 4. Loop Setup
        // ---------------------------------------------------------
        bool running = true;
        window.SetEventCallback([&](const Windowing::Event& e)
        {
            if (e.Type == Windowing::EventType::WindowClose) running = false;
            if (e.Type == Windowing::EventType::KeyPressed && e.KeyCode == 256) running = false;
        });

        // Map memory once and keep it mapped for performance (Persistent Mapping)
        auto* uboData = static_cast<Runtime::RHI::UniformBufferObject*>(uniformBuffer.Map());
        auto startTime = std::chrono::high_resolution_clock::now();

        Log::Info("Starting Render Loop...");
        while (running && !window.ShouldClose())
        {
            window.OnUpdate();

            // --- Update Logic (CPU) ---
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

            Runtime::RHI::UniformBufferObject ubo{};
            // Rotate 90 degrees per second
            ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            // Camera looking at 0,0,0 from 2,2,2
            ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(0.0f, 0.0f, 1.0f));
            // Perspective projection
            ubo.proj = glm::perspective(glm::radians(45.0f),
                                        swapchain.GetExtent().width / (float)swapchain.GetExtent().height, 0.1f, 10.0f);
            // Vulkan Y-Flip
            ubo.proj[1][1] *= -1;

            // Copy matrices to GPU
            memcpy(uboData, &ubo, sizeof(ubo));

            // --- Render Logic (GPU) ---
            renderer.BeginFrame();
            if (renderer.IsFrameInProgress())
            {
                renderer.BindPipeline(pipeline);
                renderer.SetViewport(window.GetWidth(), window.GetHeight());

                // Bind Vertex Buffer
                VkBuffer vertexBuffers[] = {vertexBuffer.GetHandle()};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(renderer.GetCommandBuffer(), 0, 1, vertexBuffers, offsets);

                // Bind Descriptor Set (The UBO)
                VkDescriptorSet descriptorSets[] = {descriptorSet};
                vkCmdBindDescriptorSets(
                    renderer.GetCommandBuffer(),
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.GetLayout(),
                    0, 1, descriptorSets, 0, nullptr
                );

                renderer.Draw(static_cast<uint32_t>(vertices.size()));
                renderer.EndFrame();
            }
        }

        uniformBuffer.Unmap();
        Log::Info("Loop finished, waiting for device idle...");
        vkDeviceWaitIdle(device.GetLogicalDevice());
    }

    vkDestroySurfaceKHR(context.GetInstance(), surface, nullptr);
    Log::Info("Shutdown complete.");
    return 0;
}
