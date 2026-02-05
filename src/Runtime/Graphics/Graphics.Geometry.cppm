// src/Runtime/Graphics/Graphics.Geometry.cppm
module;
#include <vector>
#include <span>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <queue>
#include <expected>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics:Geometry;

import RHI;
import Geometry;
import Core;

export namespace Graphics
{
    // --- Data Structures ---

    enum class PrimitiveTopology
    {
        Triangles,
        Lines,
        Points
    };

    enum class GeometryUploadMode
    {
        Staged,
        Direct
    };

    struct GeometryUploadRequest
    {
        std::span<const glm::vec3> Positions;
        std::span<const glm::vec3> Normals;
        std::span<const glm::vec4> Aux; // UVs packed in xy, Color/Data in zw
        std::span<const uint32_t> Indices;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
        GeometryUploadMode UploadMode = GeometryUploadMode::Staged;
    };

    struct GeometryCpuData
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
        std::vector<glm::vec4> Aux;
        std::vector<uint32_t> Indices;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;

        [[nodiscard]] GeometryUploadRequest ToUploadRequest(GeometryUploadMode mode = GeometryUploadMode::Staged) const
        {
            return {Positions, Normals, Aux, Indices, Topology, mode};
        }
    };

    struct GeometryCollisionData
    {
        Geometry::AABB LocalAABB;
        Geometry::Octree LocalOctree; // Static octree of mesh vertices

        // Optional: Keep positions for precise ray intersection tests after Octree broadphase
        std::vector<glm::vec3> Positions;
        std::vector<uint32_t> Indices;
    };

    struct GeometryBufferLayout
    {
        VkDeviceSize PositionsOffset = 0;
        VkDeviceSize PositionsSize = 0;

        VkDeviceSize NormalsOffset = 0;
        VkDeviceSize NormalsSize = 0;

        VkDeviceSize AuxOffset = 0;
        VkDeviceSize AuxSize = 0;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
    };

    // --- The GPU Resource ---

    class GeometryGpuData
    {
    public:
        GeometryGpuData() = default;

        ~GeometryGpuData() = default;

        [[nodiscard]] static std::pair<std::unique_ptr<GeometryGpuData>, RHI::TransferToken>
        CreateAsync(std::shared_ptr<RHI::VulkanDevice> device,
                    RHI::TransferManager& transferManager,
                    const GeometryUploadRequest& data);

        [[nodiscard]] RHI::VulkanBuffer* GetVertexBuffer() const { return m_VertexBuffer.get(); }
        [[nodiscard]] RHI::VulkanBuffer* GetIndexBuffer() const { return m_IndexBuffer.get(); }
        [[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }
        [[nodiscard]] const GeometryBufferLayout& GetLayout() const { return m_Layout; }
        [[nodiscard]] PrimitiveTopology GetTopology() const { return m_Layout.Topology; }

    private:
        std::unique_ptr<RHI::VulkanBuffer> m_VertexBuffer;
        std::unique_ptr<RHI::VulkanBuffer> m_IndexBuffer;

        GeometryBufferLayout m_Layout{};
        uint32_t m_IndexCount = 0;
    };

    // Use the core StrongHandle template for type safety and consistency

    using GeometryPool = Core::ResourcePool<GeometryGpuData, Geometry::GeometryHandle>;

    /*class GeometryStorage
    {
    public:
        // Initialize with frames-in-flight count for safe deferred deletion
        void Initialize(uint32_t framesInFlight)
        {
            m_FramesInFlight = framesInFlight;
        }

        // Adds ownership of the GPU data to the system and returns a handle
        GeometryHandle Add(std::unique_ptr<GeometryGpuData> data)
        {
            std::unique_lock lock(m_Mutex);

            uint32_t index;
            if (!m_FreeIndices.empty())
            {
                index = m_FreeIndices.front();
                m_FreeIndices.pop_front();
            }
            else
            {
                index = static_cast<uint32_t>(m_Slots.size());
                m_Slots.emplace_back();
            }

            Slot& slot = m_Slots[index];
            slot.Data = std::move(data);
            slot.Generation++; // Bump generation on allocation
            slot.IsActive = true;

            return {index, slot.Generation};
        }

        // Deferred removal: Marks the handle for deletion but doesn't free immediately.
        // The slot will be recycled after FramesInFlight frames have passed.
        void Remove(GeometryHandle handle, uint64_t currentFrameNumber)
        {
            std::unique_lock lock(m_Mutex);

            if (handle.Index >= m_Slots.size()) return;

            Slot& slot = m_Slots[handle.Index];
            if (slot.IsActive && slot.Generation == handle.Generation)
            {
                // Mark as inactive immediately so Get() returns nullptr
                slot.IsActive = false;

                // Queue for deferred destruction
                PendingKill pending;
                pending.SlotIndex = handle.Index;
                pending.Generation = handle.Generation;
                pending.KillFrameNumber = currentFrameNumber;
                m_PendingKillList.push_back(pending);
            }
        }

        // Process the pending kill list. Call this once per frame from RenderSystem.
        // Recycles slots only after enough frames have passed to ensure GPU is done.
        void ProcessDeletions(uint64_t currentFrameNumber)
        {
            std::unique_lock lock(m_Mutex);

            std::erase_if(m_PendingKillList, [&](const PendingKill& item)
            {
                // Safe to recycle when: CurrentFrame > KillFrame + FramesInFlight
                if (currentFrameNumber <= item.KillFrameNumber + m_FramesInFlight)
                    return false;

                // Verify the slot still matches (hasn't been reallocated somehow)
                Slot& slot = m_Slots[item.SlotIndex];
                if (!slot.IsActive && slot.Generation == item.Generation)
                {
                    // Now safe to destroy the GPU data and recycle the index
                    slot.Data.reset();
                    m_FreeIndices.push_back(item.SlotIndex);
                }

                // Either way, we can drop the pending entry after the fence.
                return true;
            });
        }

        /// @brief Get geometry data by handle
        /// @return Expected with pointer to data, or error if handle is invalid/stale
        [[nodiscard]] Core::Expected<GeometryGpuData*> Get(GeometryHandle handle)
        {
            std::shared_lock lock(m_Mutex);  // Use shared_lock for read-only access

            if (handle.Index >= m_Slots.size())
                return std::unexpected(Core::ErrorCode::ResourceNotFound);

            const Slot& slot = m_Slots[handle.Index];
            if (!slot.IsActive)
                return std::unexpected(Core::ErrorCode::ResourceNotFound);

            if (slot.Generation != handle.Generation)
                return std::unexpected(Core::ErrorCode::ResourceNotFound); // Stale handle

            return slot.Data.get();
        }

        /// @brief Get geometry data by handle (raw pointer version for hot paths)
        /// @return Raw pointer to data, or nullptr if invalid. Use only when you've validated the handle.
        [[nodiscard]] GeometryGpuData* GetUnchecked(GeometryHandle handle)
        {
            std::shared_lock lock(m_Mutex);
            if (handle.Index >= m_Slots.size()) return nullptr;
            const Slot& slot = m_Slots[handle.Index];
            if (slot.IsActive && slot.Generation == handle.Generation)
                return slot.Data.get();
            return nullptr;
        }

        // Returns count of pending deletions (for debugging/metrics)
        [[nodiscard]] size_t GetPendingDeletionCount() const
        {
            std::shared_lock lock(m_Mutex);
            return m_PendingKillList.size();
        }

        // Clear all geometry data immediately. Call before device destruction.
        void Clear()
        {
            std::unique_lock lock(m_Mutex);
            m_PendingKillList.clear();
            for (auto& slot : m_Slots)
            {
                slot.Data.reset();
                slot.IsActive = false;
            }
            m_Slots.clear();
            m_FreeIndices.clear();
        }

    private:
        struct Slot
        {
            std::unique_ptr<GeometryGpuData> Data;
            uint32_t Generation = 0;
            bool IsActive = false;
        };

        struct PendingKill
        {
            uint32_t SlotIndex = 0;
            uint32_t Generation = 0;
            uint64_t KillFrameNumber = 0; // The frame when Remove() was called
        };

        std::vector<Slot> m_Slots;
        std::deque<uint32_t> m_FreeIndices;
        std::vector<PendingKill> m_PendingKillList;
        mutable std::shared_mutex m_Mutex;
        uint32_t m_FramesInFlight = 2; // Default, should be initialized properly
    };*/
}
