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
import Core.Filesystem;
import Runtime.RHI.Context;
import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Renderer;
import Runtime.RHI.Shader;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Buffer;
import Runtime.RHI.CommandUtils;
import Runtime.RHI.Types;
import Runtime.RHI.Descriptors;
import Runtime.RHI.Texture;
import Runtime.ECS.Components;
import Runtime.ECS.Scene;

using namespace Core;

const std::vector<Runtime::RHI::Vertex> vertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0, 1.0}}, // 0: Top-Left (Red)
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0, 0.0}}, // 1: Top-Right (Green)
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0, 0.0}}, // 2: Bottom-Right (Blue)
    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0, 1.0}} // 3: Bottom-Left (White)
};

const std::vector<uint32_t> indices = {
    0, 1, 2, // First Triangle
    2, 3, 0 // Second Triangle
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

        VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();

        // Staging
        Runtime::RHI::VulkanBuffer indexStagingBuffer(
            device,
            indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY
        );

        void* idxData = indexStagingBuffer.Map();
        memcpy(idxData, indices.data(), (size_t)indexBufferSize);
        indexStagingBuffer.Unmap();

        // GPU Buffer (Note the usage flag: INDEX_BUFFER_BIT)
        Runtime::RHI::VulkanBuffer indexBuffer(
            device,
            indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        Runtime::RHI::CommandUtils::ExecuteImmediate(device, [&](VkCommandBuffer cmd)
        {
            VkBufferCopy copyRegion{};
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer.GetHandle(), vertexBuffer.GetHandle(), 1, &copyRegion);


            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(cmd, indexStagingBuffer.GetHandle(), indexBuffer.GetHandle(), 1, &copyRegion);
        });

        std::string texPath = Core::Filesystem::GetAssetPath("assets/textures/Checkerboard.png");
        Runtime::RHI::Texture texture(device, texPath);

        // ---------------------------------------------------------
        // 2. Uniform Buffer & Descriptor Setup
        // ---------------------------------------------------------
        Runtime::RHI::VulkanBuffer cameraBuffer(
            device,
            sizeof(Runtime::RHI::CameraBufferObject), // New Struct
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        Runtime::RHI::DescriptorLayout descriptorLayout(device);
        Runtime::RHI::DescriptorPool descriptorPool(device);

        VkDescriptorSet descriptorSet = descriptorPool.Allocate(descriptorLayout.GetHandle());

        // LINK Buffer -> Descriptor Set
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraBuffer.GetHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(Runtime::RHI::CameraBufferObject);

        VkWriteDescriptorSet descriptorWriteUBO{};
        descriptorWriteUBO.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteUBO.dstSet = descriptorSet;
        descriptorWriteUBO.dstBinding = 0;
        descriptorWriteUBO.dstArrayElement = 0;
        descriptorWriteUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWriteUBO.descriptorCount = 1;
        descriptorWriteUBO.pBufferInfo = &bufferInfo;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture.GetView();
        imageInfo.sampler = texture.GetSampler();

        VkWriteDescriptorSet descriptorWriteImage{};
        descriptorWriteImage.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteImage.dstSet = descriptorSet;
        descriptorWriteImage.dstBinding = 1; // Binding 1
        descriptorWriteImage.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWriteImage.descriptorCount = 1;
        descriptorWriteImage.pImageInfo = &imageInfo;

        std::vector<VkWriteDescriptorSet> writes = {descriptorWriteUBO, descriptorWriteImage};
        vkUpdateDescriptorSets(device.GetLogicalDevice(), 2, writes.data(), 0, nullptr);

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

        Runtime::ECS::Scene scene;

        for (int i = 0; i < 10; i++)
        {
            for (int j = 0; j < 10; j++)
            {
                auto e = scene.CreateEntity("Quad");

                auto& transform = scene.GetRegistry().get<Runtime::ECS::TransformComponent>(e);
                transform.Position = glm::vec3(i * 1.5f, j * 1.5f, -5.0f); // Spread them out
                transform.Scale = glm::vec3(1.0f);

                auto& mesh = scene.GetRegistry().emplace<Runtime::ECS::MeshComponent>(e);
                mesh.VertexBuffer = &vertexBuffer; // Point to the single shared buffer
                mesh.IndexBuffer = &indexBuffer;
                mesh.IndexCount = static_cast<uint32_t>(indices.size());
            }
        }

        // Map memory once and keep it mapped for performance (Persistent Mapping)
        auto* persitantCameraData = static_cast<Runtime::RHI::CameraBufferObject*>(cameraBuffer.Map());
        auto startTime = std::chrono::high_resolution_clock::now();

        Log::Info("Starting Render Loop...");
        while (running && !window.ShouldClose())
        {
            window.OnUpdate();

            // --- Update Logic (CPU) ---
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

            Runtime::RHI::CameraBufferObject camData{};
            camData.view = glm::lookAt(glm::vec3(5.0f, 5.0f, 10.0f), glm::vec3(5.0f, 5.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
            camData.proj = glm::perspective(glm::radians(45.0f),
                                            swapchain.GetExtent().width / (float)swapchain.GetExtent().height, 0.1f,
                                            100.0f);
            camData.proj[1][1] *= -1;
            memcpy(persitantCameraData, &camData, sizeof(camData));

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

                // NEW: Bind Index Buffer
                vkCmdBindIndexBuffer(renderer.GetCommandBuffer(), indexBuffer.GetHandle(), 0, VK_INDEX_TYPE_UINT32);

                // Bind Descriptors
                VkDescriptorSet descriptorSets[] = {descriptorSet};
                vkCmdBindDescriptorSets(
                    renderer.GetCommandBuffer(),
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.GetLayout(),
                    0, 1, descriptorSets, 0, nullptr
                );

                auto view = scene.GetRegistry().view<Runtime::ECS::TransformComponent, Runtime::ECS::MeshComponent>();

                for (auto [entity, trans, mesh] : view.each()) {
                    // 1. Calculate Model Matrix
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), trans.Position);
                    // Add rotation if you want
                    model = glm::rotate(model, time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

                    // 2. Push Constants (Fast update!)
                    Runtime::RHI::MeshPushConstants push{};
                    push.model = model;

                    vkCmdPushConstants(
                        renderer.GetCommandBuffer(),
                        pipeline.GetLayout(),
                        VK_SHADER_STAGE_VERTEX_BIT,
                        0,
                        sizeof(Runtime::RHI::MeshPushConstants),
                        &push
                    );

                    // 3. Bind Mesh Buffers (Optimization: Check if already bound to avoid redundant commands)
                    VkBuffer vBuffers[] = { mesh.VertexBuffer->GetHandle() };
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(renderer.GetCommandBuffer(), 0, 1, vBuffers, offsets);
                    vkCmdBindIndexBuffer(renderer.GetCommandBuffer(), mesh.IndexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);

                    // 4. Draw
                    vkCmdDrawIndexed(renderer.GetCommandBuffer(), mesh.IndexCount, 1, 0, 0, 0);
                }

                renderer.EndFrame();
            }
        }

        cameraBuffer.Unmap();
        Log::Info("Loop finished, waiting for device idle...");
        vkDeviceWaitIdle(device.GetLogicalDevice());
    }

    vkDestroySurfaceKHR(context.GetInstance(), surface, nullptr);
    Log::Info("Shutdown complete.");
    return 0;
}
