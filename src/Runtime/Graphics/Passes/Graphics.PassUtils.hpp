#pragma once


// =============================================================================
// PassUtils — shared implementation helpers for render passes.
//
// This header is intended for inclusion in render pass .cpp global module
// fragments only — it is not part of any exported module interface.
// =============================================================================

// no Standard-library includes allowed here — this header is included in multiple TU contexts

// =============================================================================
// CheckVkResult — unified Vulkan error logging for render passes.
// =============================================================================

inline void CheckVkResult(VkResult r, std::string_view passName, const char* what)
{
    if (r != VK_SUCCESS)
        Core::Log::Error("{}: {} failed (VkResult={})", passName, what, (int)r);
}

// =============================================================================
// CreateSSBODescriptorSetLayout — creates a single-binding SSBO layout.
// =============================================================================
// Most render passes (Line, PointCloud) use an identical descriptor set layout
// with a single STORAGE_BUFFER binding at binding 0.

inline VkDescriptorSetLayout CreateSSBODescriptorSetLayout(
    VkDevice device, VkShaderStageFlags stages, std::string_view passName)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = stages;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    CheckVkResult(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout),
                  passName, "vkCreateDescriptorSetLayout");
    return layout;
}

// =============================================================================
// CreateMultiSamplerSetLayout — N-binding combined image sampler layout.
// =============================================================================
// Creates a descriptor set layout with `bindingCount` consecutive combined
// image sampler bindings (binding 0..bindingCount-1), all with the same stage
// flags. Use this for passes that sample multiple textures in one set (e.g.,
// PostProcess tone-map: scene color + bloom; SMAA blend: edges + area + search).
//
// For single-binding layouts prefer CreateSamplerDescriptorSetLayout which
// avoids the std::vector allocation.

inline VkDescriptorSetLayout CreateMultiSamplerSetLayout(
    VkDevice device, uint32_t bindingCount, VkShaderStageFlags stages, std::string_view passName)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings(bindingCount);
    for (uint32_t i = 0; i < bindingCount; ++i)
    {
        bindings[i] = {};
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = stages;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindingCount;
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    CheckVkResult(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout),
                  passName, "vkCreateDescriptorSetLayout");
    return layout;
}

// =============================================================================
// CreateSamplerDescriptorSetLayout — single-binding combined image sampler.
// =============================================================================

inline VkDescriptorSetLayout CreateSamplerDescriptorSetLayout(
    VkDevice device, VkShaderStageFlags stages, std::string_view passName)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = stages;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    CheckVkResult(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout),
                  passName, "vkCreateDescriptorSetLayout");
    return layout;
}

// =============================================================================
// CreateNearestSampler — nearest-neighbor clamp-to-edge sampler.
// =============================================================================
// Used by DebugViewPass and SelectionOutlinePass for integer/depth textures.

inline VkSampler CreateNearestSampler(VkDevice device, std::string_view passName)
{
    VkSamplerCreateInfo samp{};
    samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp.magFilter = VK_FILTER_NEAREST;
    samp.minFilter = VK_FILTER_NEAREST;
    samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.minLod = 0.0f;
    samp.maxLod = 0.0f;
    samp.maxAnisotropy = 1.0f;

    VkSampler sampler = VK_NULL_HANDLE;
    CheckVkResult(vkCreateSampler(device, &samp, nullptr, &sampler), passName, "vkCreateSampler");
    return sampler;
}

// =============================================================================
// SetViewportScissor — set dynamic viewport and scissor state.
// =============================================================================
// Called identically in Line, PointCloud, DebugView, SelectionOutline passes.

inline void SetViewportScissor(VkCommandBuffer cmd, VkExtent2D extent)
{
    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D sc{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

// =============================================================================
// UpdateSSBODescriptor — write a buffer range to a descriptor set binding.
// =============================================================================
// Used by Line and PointCloud passes after uploading data to the SSBO.

inline void UpdateSSBODescriptor(VkDevice device, VkDescriptorSet set,
                                 uint32_t binding, VkBuffer buffer, VkDeviceSize range)
{
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = buffer;
    bufInfo.offset = 0;
    bufInfo.range = range;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

// =============================================================================
// UpdateImageDescriptor — write an image view + sampler to a descriptor set.
// =============================================================================

inline void UpdateImageDescriptor(VkDevice device, VkDescriptorSet set,
                                  uint32_t binding, VkSampler sampler,
                                  VkImageView view, VkImageLayout layout)
{
    VkDescriptorImageInfo imageInfo{sampler, view, layout};

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

// =============================================================================
// TransitionImageToShaderRead — single-time barrier for dummy images.
// =============================================================================
// Used in Initialize() by DebugView and SelectionOutline to transition dummy
// images from UNDEFINED to SHADER_READ_ONLY_OPTIMAL.

inline void TransitionImageToShaderRead(
    RHI::VulkanDevice& device,
    VkImage image,
    VkImageAspectFlags aspect,
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
{
    VkCommandBuffer cmd = RHI::CommandUtils::BeginSingleTimeCommands(device);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    RHI::CommandUtils::EndSingleTimeCommands(device, cmd);
}

// =============================================================================
// EnsurePerFrameBuffer<T>
// =============================================================================
// Grows a per-frame host-visible SSBO array to hold at least `required`
// elements of type T.  Returns true on success, false on allocation failure.
//
// Parameters:
//   device        — Vulkan device (borrowed).
//   buffers       — array of FRAMES unique_ptr<VulkanBuffer>, indexed by frame.
//   capacity      — in/out: current capacity in elements (updated on growth).
//   required      — minimum element count needed this frame.
//   minCapacity   — initial allocation floor (avoids tiny first allocations).
//   passName      — used in error log messages.
//
// Growth strategy: next power of 2 >= required, floored at minCapacity.
//
// bufferUsage — Vulkan buffer usage flags. Default is STORAGE_BUFFER_BIT.
//               Pass STORAGE_BUFFER_BIT | SHADER_DEVICE_ADDRESS_BIT for
//               transient BDA-addressable buffers (Line/Point passes).
template<typename T, uint32_t FRAMES>
bool EnsurePerFrameBuffer(RHI::VulkanDevice& device,
                          std::unique_ptr<RHI::VulkanBuffer> (&buffers)[FRAMES],
                          uint32_t& capacity,
                          uint32_t  required,
                          uint32_t  minCapacity,
                          std::string_view passName,
                          VkBufferUsageFlags bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
{
    if (required <= capacity && buffers[0] != nullptr)
        return true;

    uint32_t newCapacity = minCapacity;
    while (newCapacity < required)
        newCapacity *= 2;

    const size_t byteSize = static_cast<size_t>(newCapacity) * sizeof(T);

    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        // Defer destruction of old buffer — it may still be referenced by
        // an in-flight command buffer on a previous frame.  The unique_ptr
        // is moved into the SafeDestroy lambda; the buffer is freed once
        // the GPU timeline confirms the previous frame has completed.
        if (buffers[i])
            device.SafeDestroy([old = std::move(buffers[i])]() { /* freed on lambda destruction */ });

        buffers[i] = std::make_unique<RHI::VulkanBuffer>(
            device,
            byteSize,
            bufferUsage,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        if (!buffers[i]->GetMappedData())
        {
            Core::Log::Error("{}: Failed to allocate SSBO ({} bytes)", passName, byteSize);
            return false;
        }
    }

    capacity = newCapacity;
    return true;
}

// =============================================================================
// MakeDeviceAlias — non-owning shared_ptr wrapper for VulkanDevice.
// =============================================================================
// PipelineBuilder takes a std::shared_ptr<VulkanDevice> but render passes only
// borrow the device. This creates a no-op deleter alias for use in BuildPipeline.

inline std::shared_ptr<RHI::VulkanDevice> MakeDeviceAlias(RHI::VulkanDevice* device)
{
    return std::shared_ptr<RHI::VulkanDevice>(device, [](RHI::VulkanDevice*) {});
}

// =============================================================================
// AllocatePerFrameSets<N> — fill a fixed-size array with per-frame descriptor sets.
// =============================================================================
// Allocates N descriptor sets from the pool, all using the same layout.
// Use in Initialize() to replace the manual for loop.

template<uint32_t N>
inline void AllocatePerFrameSets(RHI::DescriptorAllocator& pool,
                                  VkDescriptorSetLayout layout,
                                  VkDescriptorSet (&sets)[N])
{
    for (uint32_t i = 0; i < N; ++i)
        sets[i] = pool.Allocate(layout);
}

// =============================================================================
// FrustumCullSphere — CPU-side sphere-vs-frustum visibility test.
// =============================================================================
// Transforms a local-space bounding sphere into world space using the entity's
// model matrix, then tests against 6 camera frustum planes.
// Returns true if the sphere is visible (overlaps the frustum).
//
// This mirrors the GPU culling logic in instance_cull.comp:
//   worldCenter = Model * localCenter
//   worldRadius = localRadius * max(scaleX, scaleY, scaleZ)
//   for each plane: if dot(normal, worldCenter) + d < -worldRadius → culled
//
// Requires: import Geometry; in the including TU (for Frustum, Sphere, TestOverlap).

inline bool FrustumCullSphere(const glm::mat4& worldMatrix,
                               const glm::vec4& localBounds,
                               const Geometry::Frustum& frustum)
{
    const float localRadius = localBounds.w;
    if (localRadius <= 0.0f)
        return false; // Empty/inactive geometry — treat as culled.

    const glm::vec3 localCenter{localBounds.x, localBounds.y, localBounds.z};
    const glm::vec3 worldCenter = glm::vec3(worldMatrix * glm::vec4(localCenter, 1.0f));

    // Scale radius by maximum axis scale of the model matrix.
    const float sx = glm::length(glm::vec3(worldMatrix[0]));
    const float sy = glm::length(glm::vec3(worldMatrix[1]));
    const float sz = glm::length(glm::vec3(worldMatrix[2]));
    const float worldRadius = localRadius * glm::max(sx, glm::max(sy, sz));

    return Geometry::TestOverlap(frustum, Geometry::Sphere{worldCenter, worldRadius});
}

// =============================================================================
// ResolveShaderPaths — resolve a vert/frag shader pair via the ShaderRegistry.
// =============================================================================
// Combines the two repeated ResolveShaderPathOrExit calls found in every pass
// BuildPipeline() into a single expression.

inline std::pair<std::string, std::string> ResolveShaderPaths(
    const Graphics::ShaderRegistry& reg,
    Core::Hash::StringID vertId,
    Core::Hash::StringID fragId)
{
    // Must match Core::Filesystem::ShaderPathLookup signature:
    //   std::optional<std::string>(Core::Hash::StringID)
    auto resolver = [&](Core::Hash::StringID id) { return reg.Get(id); };
    return {
        Core::Filesystem::ResolveShaderPathOrExit(resolver, vertId),
        Core::Filesystem::ResolveShaderPathOrExit(resolver, fragId)
    };
}

// =============================================================================
// CreateCullingFrustum — extract camera frustum for CPU-side culling.
// =============================================================================
// Returns a frustum from the camera VP matrix, or an empty frustum when
// culling is disabled. Deduplicates the identical pattern found in every
// retained-mode render pass (Surface, Line, Point).

inline Geometry::Frustum CreateCullingFrustum(
    const glm::mat4& cameraProj,
    const glm::mat4& cameraView,
    bool cullingEnabled)
{
    return cullingEnabled
        ? Geometry::Frustum::CreateFromMatrix(cameraProj * cameraView)
        : Geometry::Frustum{};
}

// =============================================================================
// CleanupOrphanedBuffers — remove stale entries from a per-entity buffer map.
// =============================================================================
// Removes entries from `bufferMap` whose keys are not present in the sorted
// `activeKeys` vector. Deferred-destroys the GPU buffers via SafeDestroy.
//
// EntryT must have: std::unique_ptr<RHI::VulkanBuffer> Buffer;
//
// Callers must sort activeKeys before passing them in.

template<typename EntryT>
void CleanupOrphanedBuffers(
    RHI::VulkanDevice& device,
    std::unordered_map<uint32_t, EntryT>& bufferMap,
    std::vector<uint32_t>& activeKeys)
{
    if (bufferMap.empty())
        return;

    std::sort(activeKeys.begin(), activeKeys.end());

    for (auto it = bufferMap.begin(); it != bufferMap.end(); )
    {
        if (!std::binary_search(activeKeys.begin(), activeKeys.end(), it->first))
        {
            if (it->second.Buffer)
                device.SafeDestroy([old = std::move(it->second.Buffer)]() {});
            it = bufferMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// =============================================================================
// EnsurePerEntityBuffer<T> — create or update a persistent BDA-addressable
// per-entity attribute buffer.
// =============================================================================
// Deduplicates the identical pattern used by LinePass (edge aux), PointPass
// (point aux, point radii), and SurfacePass (face attr, vertex attr).
//
// EntryT must have:
//   std::unique_ptr<RHI::VulkanBuffer> Buffer;
//   uint32_t Count;
//
// Returns the buffer device address, or 0 on allocation failure.

template<typename T, typename EntryT>
uint64_t EnsurePerEntityBuffer(
    RHI::VulkanDevice& device,
    std::unordered_map<uint32_t, EntryT>& bufferMap,
    uint32_t entityKey,
    const T* data,
    uint32_t elementCount,
    std::string_view passName)
{
    auto it = bufferMap.find(entityKey);

    // Buffer exists and count matches — update data in-place.
    if (it != bufferMap.end() && it->second.Count == elementCount && it->second.Buffer)
    {
        it->second.Buffer->Write(data, static_cast<size_t>(elementCount) * sizeof(T));
        return it->second.Buffer->GetDeviceAddress();
    }

    // Need to create or recreate (count changed).
    if (it != bufferMap.end() && it->second.Buffer)
    {
        device.SafeDestroy([old = std::move(it->second.Buffer)]() {});
        it->second.Count = 0;
    }

    const VkDeviceSize size = static_cast<VkDeviceSize>(elementCount) * sizeof(T);
    auto buf = std::make_unique<RHI::VulkanBuffer>(
        device, size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    if (!buf->GetMappedData())
    {
        Core::Log::Error("{}: Failed to allocate per-entity buffer ({} bytes)", passName, size);
        return 0;
    }

    buf->Write(data, static_cast<size_t>(size));
    const uint64_t addr = buf->GetDeviceAddress();

    bufferMap[entityKey] = { std::move(buf), elementCount };
    return addr;
}

// =============================================================================
// EnsureSingleBuffer<T> — grow a single (non-per-frame) host-visible BDA buffer.
// =============================================================================
// Same growth strategy as EnsurePerFrameBuffer but for a single buffer instance.
// Used for shared-across-frames transient data (e.g., LinePass identity edge buffer).
//
// Returns true on success, false on allocation failure.

template<typename T>
bool EnsureSingleBuffer(RHI::VulkanDevice& device,
                        std::unique_ptr<RHI::VulkanBuffer>& buffer,
                        uint32_t& capacity,
                        uint32_t  required,
                        uint32_t  minCapacity,
                        std::string_view passName,
                        VkBufferUsageFlags bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
{
    if (required <= capacity && buffer != nullptr)
        return true;

    uint32_t newCapacity = minCapacity;
    while (newCapacity < required)
        newCapacity *= 2;

    if (buffer)
        device.SafeDestroy([old = std::move(buffer)]() {});

    const size_t byteSize = static_cast<size_t>(newCapacity) * sizeof(T);
    buffer = std::make_unique<RHI::VulkanBuffer>(
        device, byteSize, bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU);

    if (!buffer->GetMappedData())
    {
        Core::Log::Error("{}: Failed to allocate buffer ({} bytes)", passName, byteSize);
        return false;
    }

    capacity = newCapacity;
    return true;
}

// =============================================================================
// ClampLineWidth / ClampPointSize — numerical safeguard helpers.
// =============================================================================
// Centralize the clamping constants documented in CLAUDE.md (Numerical Safeguards):
//   Line widths: [0.5, 32.0] pixels (C++ and shader).
//   Point radii:  [0.0001, 1.0] world-space.

inline float ClampLineWidth(float width)  { return std::clamp(width, 0.5f, 32.0f); }
inline float ClampPointSize(float size)   { return std::clamp(size, 0.0001f, 1.0f); }
