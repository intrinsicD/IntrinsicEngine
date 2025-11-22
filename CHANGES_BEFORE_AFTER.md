diff --git a/CHANGES_BEFORE_AFTER.md b/CHANGES_BEFORE_AFTER.md
new file mode 100644
index 0000000..e69de29
diff --git a/src/Core/Core.Memory.cpp b/src/Core/Core.Memory.cpp
index f142987..c50a3a8 100644
--- a/src/Core/Core.Memory.cpp
+++ b/src/Core/Core.Memory.cpp
@@ -5,21 +5,35 @@ module; // <--- Start Global Fragment
 #include <cstring>
 #include <new>
 #include <memory>
+#include <limits>
 #include <expected>
 
 module Core.Memory; // <--- Enter Module Purview
 
 namespace Core::Memory {
 
+    namespace
+    {
+        constexpr size_t AlignUp(size_t value, size_t alignment)
+        {
+            return (value + alignment - 1) & ~(alignment - 1);
+        }
+    }
+
     LinearArena::LinearArena(size_t sizeBytes)
-        : totalSize_(sizeBytes)
+        : totalSize_(AlignUp(sizeBytes, CACHE_LINE))
         , offset_(0)
     {
 #if defined(_MSC_VER)
-        start_ = static_cast<std::byte*>(_aligned_malloc(sizeBytes, CACHE_LINE));
+        start_ = static_cast<std::byte*>(_aligned_malloc(totalSize_, CACHE_LINE));
 #else
-        start_ = static_cast<std::byte*>(std::aligned_alloc(CACHE_LINE, sizeBytes));
+        start_ = static_cast<std::byte*>(std::aligned_alloc(CACHE_LINE, totalSize_));
 #endif
+
+        if (!start_)
+        {
+            totalSize_ = 0;
+        }
     }
 
     LinearArena::~LinearArena() {
@@ -33,15 +47,28 @@ namespace Core::Memory {
     }
 
     std::expected<void*, AllocatorError> LinearArena::Alloc(size_t size, size_t align) {
+        if (!start_)
+        {
+            return std::unexpected(AllocatorError::OutOfMemory);
+        }
+
+        const size_t safeAlign = align == 0 ? 1 : align;
         uintptr_t currentPtr = reinterpret_cast<uintptr_t>(start_ + offset_);
-        uintptr_t alignedPtr = (currentPtr + (align - 1)) & ~(align - 1);
+        uintptr_t alignedPtr = (currentPtr + (safeAlign - 1)) & ~(safeAlign - 1);
         size_t padding = alignedPtr - currentPtr;
 
-        if (offset_ + padding + size > totalSize_) {
+        if (padding > (std::numeric_limits<size_t>::max() - offset_))
+        {
+            return std::unexpected(AllocatorError::OutOfMemory);
+        }
+
+        const size_t newOffset = offset_ + padding;
+        if (size > (std::numeric_limits<size_t>::max() - newOffset) || newOffset + size > totalSize_)
+        {
             return std::unexpected(AllocatorError::OutOfMemory);
         }
 
-        offset_ += padding;
+        offset_ = newOffset;
         void* ptr = start_ + offset_;
         offset_ += size;
 
diff --git a/src/Core/Core.Tasks.cpp b/src/Core/Core.Tasks.cpp
index a0f5b38..a9c85d4 100644
--- a/src/Core/Core.Tasks.cpp
+++ b/src/Core/Core.Tasks.cpp
@@ -55,6 +55,8 @@ namespace Core::Tasks
     {
         if (!s_Ctx) return;
 
+        WaitForAll();
+
         {
             std::lock_guard lock(s_Ctx->queueMutex);
             s_Ctx->isRunning = false;
@@ -73,6 +75,12 @@ namespace Core::Tasks
 
     void Scheduler::Dispatch(TaskFunction&& task)
     {
+        if (!s_Ctx)
+        {
+            Log::Error("Scheduler::Dispatch called before Initialize");
+            return;
+        }
+
         {
             std::lock_guard lock(s_Ctx->queueMutex);
             s_Ctx->globalQueue.push_back(std::move(task));
@@ -83,6 +91,12 @@ namespace Core::Tasks
 
     void Scheduler::WaitForAll()
     {
+        if (!s_Ctx)
+        {
+            Log::Error("Scheduler::WaitForAll called before Initialize");
+            return;
+        }
+
         std::unique_lock lock(s_Ctx->queueMutex);
         s_Ctx->waitCondition.wait(lock, []
         {
diff --git a/src/Runtime/Graphics/Graphics.ModelLoader.cpp b/src/Runtime/Graphics/Graphics.ModelLoader.cpp
index 865de12..bff4c90 100644
--- a/src/Runtime/Graphics/Graphics.ModelLoader.cpp
+++ b/src/Runtime/Graphics/Graphics.ModelLoader.cpp
@@ -5,6 +5,7 @@ module;
 #include <glm/gtc/type_ptr.hpp>
 #include <vector>
 #include <memory>
+#include <cstring>
 
 module Runtime.Graphics.ModelLoader;
 import Core.Logging;
@@ -50,6 +51,37 @@ namespace Runtime::Graphics
                 std::vector<RHI::Vertex> vertices;
                 std::vector<uint32_t> indices;
 
+                auto validateAccessor = [&](const tinygltf::Accessor& accessor, int expectedType, int expectedComponentType,
+                                            size_t expectedElementSize) -> bool
+                {
+                    if (accessor.type != expectedType || accessor.componentType != expectedComponentType)
+                    {
+                        Core::Log::Error("Unsupported accessor layout: type {} component {}", accessor.type,
+                                         accessor.componentType);
+                        return false;
+                    }
+
+                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
+                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
+
+                    size_t stride = bufferView.byteStride == 0 ? expectedElementSize : bufferView.byteStride;
+                    if (stride != expectedElementSize)
+                    {
+                        Core::Log::Error("Unexpected stride {} for accessor; expected {}", stride, expectedElementSize);
+                        return false;
+                    }
+
+                    const size_t start = bufferView.byteOffset + accessor.byteOffset;
+                    const size_t end = start + stride * accessor.count;
+                    if (end > buffer.data.size())
+                    {
+                        Core::Log::Error("Accessor out of bounds (end {}, buffer size {})", end, buffer.data.size());
+                        return false;
+                    }
+
+                    return true;
+                };
+
                 // --- 1. Get Attributes (Pos, Normal, UV) ---
                 const float* positionBuffer = nullptr;
                 const float* normalsBuffer = nullptr;
@@ -60,10 +92,16 @@ namespace Runtime::Graphics
                 if (primitive.attributes.find("POSITION") != primitive.attributes.end())
                 {
                     const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("POSITION")];
+                    if (!validateAccessor(accessor, TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT,
+                                          sizeof(float) * 3))
+                    {
+                        continue;
+                    }
+
                     const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                     const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
-                    positionBuffer = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.
-                        byteOffset]);
+                    positionBuffer = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.
+                        byteOffset);
                     vertexCount = accessor.count;
                 }
 
@@ -71,20 +109,32 @@ namespace Runtime::Graphics
                 if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
                 {
                     const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("NORMAL")];
+                    if (!validateAccessor(accessor, TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT,
+                                          sizeof(float) * 3))
+                    {
+                        continue;
+                    }
+
                     const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                     const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
-                    normalsBuffer = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.
-                        byteOffset]);
+                    normalsBuffer = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.
+                        byteOffset);
                 }
 
                 // TexCoords
                 if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
                 {
                     const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
+                    if (!validateAccessor(accessor, TINYGLTF_TYPE_VEC2, TINYGLTF_COMPONENT_TYPE_FLOAT,
+                                          sizeof(float) * 2))
+                    {
+                        continue;
+                    }
+
                     const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                     const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
-                    texCoordsBuffer = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.
-                        byteOffset]);
+                    texCoordsBuffer = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.
+                        byteOffset);
                 }
 
                 // Assemble Vertices
@@ -121,19 +171,53 @@ namespace Runtime::Graphics
                     const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                     const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
 
-                    const void* dataPtr = &buffer.data[bufferView.byteOffset + accessor.byteOffset];
+                    if (accessor.type != TINYGLTF_TYPE_SCALAR)
+                    {
+                        Core::Log::Error("Index accessor must be scalar type");
+                        continue;
+                    }
 
+                    size_t indexSize = 0;
                     if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
+                    {
+                        indexSize = sizeof(uint32_t);
+                    }
+                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
+                    {
+                        indexSize = sizeof(uint16_t);
+                    }
+                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
+                    {
+                        indexSize = sizeof(uint8_t);
+                    }
+                    else
+                    {
+                        Core::Log::Error("Unsupported index component type {}", accessor.componentType);
+                        continue;
+                    }
+
+                    size_t stride = bufferView.byteStride == 0 ? indexSize : bufferView.byteStride;
+                    const size_t start = bufferView.byteOffset + accessor.byteOffset;
+                    const size_t end = start + stride * accessor.count;
+                    if (stride != indexSize || end > buffer.data.size())
+                    {
+                        Core::Log::Error("Invalid index buffer stride {} or bounds (end {}, size {})", stride, end,
+                                         buffer.data.size());
+                        continue;
+                    }
+
+                    const void* dataPtr = buffer.data.data() + start;
+                    if (indexSize == sizeof(uint32_t))
                     {
                         const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
                         for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                     }
-                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
+                    else if (indexSize == sizeof(uint16_t))
                     {
                         const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
                         for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                     }
-                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
+                    else
                     {
                         const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
                         for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
diff --git a/src/Runtime/Graphics/Graphics.RenderSystem.cpp b/src/Runtime/Graphics/Graphics.RenderSystem.cpp
index 34c9309..47401a1 100644
--- a/src/Runtime/Graphics/Graphics.RenderSystem.cpp
+++ b/src/Runtime/Graphics/Graphics.RenderSystem.cpp
@@ -32,19 +32,25 @@ namespace Runtime::Graphics
         // 2. Calculate aligned size for ONE frame
         size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
         size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);
+        m_CameraStride = alignedSize;
 
         // Create the UBO once here
-        m_GlobalUBO = new RHI::VulkanBuffer(
+        m_GlobalUBO = std::make_unique<RHI::VulkanBuffer>(
             device,
             alignedSize * RHI::SimpleRenderer::MAX_FRAMES_IN_FLIGHT,
             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
             VMA_MEMORY_USAGE_CPU_TO_GPU
         );
+
+        m_MappedCameraPtr = static_cast<char*>(m_GlobalUBO->Map());
     }
 
     RenderSystem::~RenderSystem()
     {
-        delete m_GlobalUBO;
+        if (m_GlobalUBO && m_MappedCameraPtr)
+        {
+            m_GlobalUBO->Unmap();
+        }
     }
 
     void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraData& camera)
@@ -57,18 +63,14 @@ namespace Runtime::Graphics
             RHI::CameraBufferObject ubo{};
             ubo.view = camera.View;
             ubo.proj = camera.Proj;
-
-            size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
-            size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);
+            const size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
 
             // Offset based on CURRENT FRAME
             uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();
-            size_t offset = frameIndex * alignedSize;
+            size_t offset = frameIndex * m_CameraStride;
 
             // Write to specific offset
-            char* data = (char*)m_GlobalUBO->Map();
-            memcpy(data + offset, &ubo, cameraDataSize);
-            m_GlobalUBO->Unmap();
+            std::memcpy(m_MappedCameraPtr + offset, &ubo, cameraDataSize);
 
             m_Renderer.BindPipeline(m_Pipeline);
 
diff --git a/src/Runtime/Graphics/Graphics.RenderSystem.cppm b/src/Runtime/Graphics/Graphics.RenderSystem.cppm
index 0118465..6ee8f6f 100644
--- a/src/Runtime/Graphics/Graphics.RenderSystem.cppm
+++ b/src/Runtime/Graphics/Graphics.RenderSystem.cppm
@@ -1,5 +1,6 @@
 module;
 #include <glm/glm.hpp>
+#include <memory>
 
 export module Runtime.Graphics.RenderSystem;
 
@@ -29,7 +30,7 @@ export namespace Runtime::Graphics
 
         void OnUpdate(ECS::Scene& scene, const CameraData& camera);
 
-        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO; }
+        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }
 
     private:
         size_t m_MinUboAlignment = 0;
@@ -40,6 +41,8 @@ export namespace Runtime::Graphics
         RHI::GraphicsPipeline& m_Pipeline;
 
         // The Global Camera UBO
-        RHI::VulkanBuffer* m_GlobalUBO;
+        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;
+        char* m_MappedCameraPtr = nullptr;
+        size_t m_CameraStride = 0;
     };
 }
diff --git a/src/Runtime/RHI/RHI.Renderer.cpp b/src/Runtime/RHI/RHI.Renderer.cpp
index 2936c1b..7d8a21b 100644
--- a/src/Runtime/RHI/RHI.Renderer.cpp
+++ b/src/Runtime/RHI/RHI.Renderer.cpp
@@ -227,6 +227,13 @@ namespace Runtime::RHI
         vkCmdDraw(m_CommandBuffers[m_CurrentFrame], vertexCount, 1, 0, 0);
     }
 
+    void SimpleRenderer::OnResize()
+    {
+        vkDeviceWaitIdle(m_Device.GetLogicalDevice());
+        m_Swapchain.Recreate();
+        CreateDepthBuffer();
+    }
+
     void SimpleRenderer::CreateDepthBuffer()
     {
         if (m_DepthImage) delete m_DepthImage;
diff --git a/src/Runtime/RHI/RHI.Renderer.cppm b/src/Runtime/RHI/RHI.Renderer.cppm
index 4040f4a..68e85ad 100644
--- a/src/Runtime/RHI/RHI.Renderer.cppm
+++ b/src/Runtime/RHI/RHI.Renderer.cppm
@@ -28,6 +28,8 @@ namespace Runtime::RHI
         void Draw(uint32_t vertexCount);
         void SetViewport(uint32_t width, uint32_t height);
 
+        void OnResize();
+
         [[nodiscard]] bool IsFrameInProgress() const { return m_IsFrameStarted; }
         [[nodiscard]] VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
         [[nodiscard]] uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
diff --git a/src/Runtime/Runtime.Engine.cpp b/src/Runtime/Runtime.Engine.cpp
index 4fa2ef2..437319c 100644
--- a/src/Runtime/Runtime.Engine.cpp
+++ b/src/Runtime/Runtime.Engine.cpp
@@ -26,6 +26,7 @@ namespace Runtime
         {
             if (e.Type == Core::Windowing::EventType::WindowClose) m_Running = false;
             if (e.Type == Core::Windowing::EventType::KeyPressed && e.KeyCode == 256) m_Running = false;
+            if (e.Type == Core::Windowing::EventType::WindowResize) m_FramebufferResized = true;
         });
 
         // 2. Vulkan Context & Surface
@@ -95,6 +96,12 @@ namespace Runtime
         {
             m_Window->OnUpdate();
 
+            if (m_FramebufferResized)
+            {
+                m_Renderer->OnResize();
+                m_FramebufferResized = false;
+            }
+
             auto currentTime = std::chrono::high_resolution_clock::now();
             float rawDt = std::chrono::duration<float>(currentTime - lastTime).count();
             lastTime = currentTime;
@@ -111,6 +118,8 @@ namespace Runtime
 
             OnUpdate(dt); // User Logic
 
+            OnRender();
+
             // Currently RenderSystem::OnUpdate handles the draw, so OnRender is optional hook
             // In future, OnRender might manipulate the RenderGraph
         }
diff --git a/src/Runtime/Runtime.Engine.cppm b/src/Runtime/Runtime.Engine.cppm
index 49bffdf..82d48c8 100644
--- a/src/Runtime/Runtime.Engine.cppm
+++ b/src/Runtime/Runtime.Engine.cppm
@@ -65,6 +65,7 @@ export namespace Runtime
         std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;
 
         bool m_Running = true;
+        bool m_FramebufferResized = false;
 
         void InitVulkan();
         void InitPipeline();
