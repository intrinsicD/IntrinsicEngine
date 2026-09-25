// Retains GPU property buffers for visualization packets across frame slots.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module Extrinsic.Graphics.VisualizationPropertyBufferResidency;

import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.BufferManager;

extern "C++" { namespace Extrinsic::RHI { class IDevice; } }

export namespace Extrinsic::Graphics
{
    class VisualizationPropertyBufferResidency final
    {
    public:
        VisualizationPropertyBufferResidency(RHI::IDevice& device,
                                             RHI::BufferManager& bufferManager);
        ~VisualizationPropertyBufferResidency();

        VisualizationPropertyBufferResidency(
            const VisualizationPropertyBufferResidency&) = delete;
        VisualizationPropertyBufferResidency& operator=(
            const VisualizationPropertyBufferResidency&) = delete;

        // Uploads changed descriptors into fresh buffers, reuses unchanged
        // ones, and evicts keys missing from `descriptors`. Payload bytes are
        // read only during this call.
        [[nodiscard]] VisualizationPropertyBufferDiagnostics Update(
            std::span<const VisualizationPropertyBufferUploadDescriptor> descriptors);

        [[nodiscard]] const VisualizationPropertyBufferAddress* Find(
            std::string_view sourceKey) const noexcept;

        [[nodiscard]] std::span<const VisualizationPropertyBufferAddress>
        GetLastAddresses() const noexcept;

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept;
        [[nodiscard]] std::size_t GetResidentBufferCount() const noexcept
        {
            return m_Entries.size();
        }

        void Clear() noexcept;

    private:
        struct Entry
        {
            std::optional<RHI::BufferManager::BufferLease> Lease{};
            std::uint64_t CapacityBytes{0u};
            VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
            VisualizationValueType ValueType{VisualizationValueType::ScalarFloat};
            std::uint32_t ElementCount{0u};
            std::uint32_t StrideBytes{0u};
            std::uint64_t DirtyStamp{0u};
            std::uint64_t SourceLayoutStamp{0u};
            std::uint64_t BufferBDA{0u};
            std::uint64_t LastSubmittedEpoch{0u};
        };

        [[nodiscard]] static bool Reusable(
            const Entry& entry,
            const VisualizationPropertyBufferUploadDescriptor& descriptor) noexcept;

        [[nodiscard]] static VisualizationPropertyBufferAddress MakeAddress(
            const std::string& sourceKey,
            const Entry& entry);

        RHI::IDevice* m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};
        std::unordered_map<std::string, Entry> m_Entries{};
        std::vector<VisualizationPropertyBufferAddress> m_LastAddresses{};
        std::uint64_t m_BufferAllocationCount{0u};
        std::uint64_t m_UpdateEpoch{0u};
    };
}
