module;

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Graphics.VisualizationPropertyBufferResidency;

import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    VisualizationPropertyBufferResidency::VisualizationPropertyBufferResidency(
        RHI::IDevice& device,
        RHI::BufferManager& bufferManager)
        : m_Device(&device)
        , m_BufferManager(&bufferManager)
    {
    }

    VisualizationPropertyBufferResidency::~VisualizationPropertyBufferResidency() = default;

    bool VisualizationPropertyBufferResidency::Reusable(
        const Entry& entry,
        const VisualizationPropertyBufferUploadDescriptor& descriptor) noexcept
    {
        return descriptor.DirtyStamp > 0u &&
               entry.DirtyStamp == descriptor.DirtyStamp &&
               entry.Domain == descriptor.Domain &&
               entry.ValueType == descriptor.ValueType &&
               entry.ElementCount == descriptor.ElementCount &&
               entry.StrideBytes == descriptor.StrideBytes &&
               entry.BufferBDA != 0u &&
               entry.Lease.has_value() &&
               entry.CapacityBytes >= descriptor.Bytes.size();
    }

    VisualizationPropertyBufferAddress VisualizationPropertyBufferResidency::MakeAddress(
        const std::string& sourceKey,
        const Entry& entry)
    {
        return VisualizationPropertyBufferAddress{
            .SourceKey = sourceKey,
            .Domain = entry.Domain,
            .ValueType = entry.ValueType,
            .ElementCount = entry.ElementCount,
            .StrideBytes = entry.StrideBytes,
            .DirtyStamp = entry.DirtyStamp,
            .BufferBDA = entry.BufferBDA,
        };
    }

    VisualizationPropertyBufferDiagnostics VisualizationPropertyBufferResidency::Update(
        const std::span<const VisualizationPropertyBufferUploadDescriptor> descriptors)
    {
        VisualizationPropertyBufferDiagnostics diagnostics{};
        m_LastAddresses.clear();
        m_LastAddresses.reserve(descriptors.size());

        for (const VisualizationPropertyBufferUploadDescriptor& descriptor : descriptors)
        {
            if (!ValidateVisualizationPropertyBufferUploadDescriptor(
                    descriptor, diagnostics))
            {
                continue;
            }

            Entry& entry = m_Entries[descriptor.SourceKey];
            if (entry.DirtyStamp > 0u &&
                descriptor.DirtyStamp > 0u &&
                descriptor.DirtyStamp < entry.DirtyStamp)
            {
                ++diagnostics.StaleDirtyStampCount;
                diagnostics.HasErrors = true;
                continue;
            }

            if (Reusable(entry, descriptor))
            {
                ++diagnostics.ReusedBufferCount;
                m_LastAddresses.push_back(
                    MakeAddress(descriptor.SourceKey, entry));
                continue;
            }

            if (!m_Device->IsOperational())
            {
                ++diagnostics.UploadDeferralCount;
                diagnostics.HasErrors = true;
                continue;
            }

            const std::uint64_t requestedBytes =
                static_cast<std::uint64_t>(descriptor.Bytes.size());
            if (!entry.Lease.has_value() || entry.CapacityBytes < requestedBytes)
            {
                entry.Lease.reset();
                entry.CapacityBytes = 0u;
                entry.BufferBDA = 0u;

                RHI::BufferDesc desc{};
                desc.SizeBytes = requestedBytes;
                desc.Usage = RHI::BufferUsage::Storage |
                             RHI::BufferUsage::TransferDst;
                desc.HostVisible = true;
                desc.DebugName = "Visualization.PropertyBuffer";

                auto lease = m_BufferManager->Create(desc);
                if (!lease.has_value())
                {
                    ++diagnostics.InvalidResourceCount;
                    diagnostics.HasErrors = true;
                    continue;
                }

                entry.Lease.emplace(std::move(*lease));
                entry.CapacityBytes = requestedBytes;
                ++m_BufferAllocationCount;
            }

            const RHI::BufferHandle handle = entry.Lease->GetHandle();
            m_Device->WriteBuffer(handle,
                                  descriptor.Bytes.data(),
                                  requestedBytes,
                                  0u);

            entry.Domain = descriptor.Domain;
            entry.ValueType = descriptor.ValueType;
            entry.ElementCount = descriptor.ElementCount;
            entry.StrideBytes = descriptor.StrideBytes;
            entry.DirtyStamp = descriptor.DirtyStamp;
            entry.BufferBDA = m_Device->GetBufferDeviceAddress(handle);

            if (entry.BufferBDA == 0u)
            {
                ++diagnostics.InvalidResourceCount;
                diagnostics.HasErrors = true;
                continue;
            }

            ++diagnostics.UploadedBufferCount;
            m_LastAddresses.push_back(MakeAddress(descriptor.SourceKey, entry));
        }

        return diagnostics;
    }

    const VisualizationPropertyBufferAddress* VisualizationPropertyBufferResidency::Find(
        const std::string_view sourceKey) const noexcept
    {
        for (const VisualizationPropertyBufferAddress& address : m_LastAddresses)
        {
            if (std::string_view{address.SourceKey} == sourceKey)
            {
                return &address;
            }
        }
        return nullptr;
    }

    std::span<const VisualizationPropertyBufferAddress>
    VisualizationPropertyBufferResidency::GetLastAddresses() const noexcept
    {
        return m_LastAddresses;
    }

    std::uint64_t VisualizationPropertyBufferResidency::GetBufferAllocationCount() const noexcept
    {
        return m_BufferAllocationCount;
    }

    void VisualizationPropertyBufferResidency::Clear() noexcept
    {
        m_LastAddresses.clear();
        m_Entries.clear();
        m_BufferAllocationCount = 0u;
    }
}
