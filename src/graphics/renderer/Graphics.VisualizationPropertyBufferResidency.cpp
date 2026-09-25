module;

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Graphics.VisualizationPropertyBufferResidency;

import Extrinsic.RHI.Device;

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
               entry.SourceLayoutStamp == descriptor.SourceLayoutStamp &&
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
            .SourceLayoutStamp = entry.SourceLayoutStamp,
            .BufferBDA = entry.BufferBDA,
        };
    }

    VisualizationPropertyBufferDiagnostics VisualizationPropertyBufferResidency::Update(
        const std::span<const VisualizationPropertyBufferUploadDescriptor> descriptors)
    {
        VisualizationPropertyBufferDiagnostics diagnostics{};
        m_LastAddresses.clear();
        m_LastAddresses.reserve(descriptors.size());
        ++m_UpdateEpoch;

        for (const VisualizationPropertyBufferUploadDescriptor& descriptor : descriptors)
        {
            if (!ValidateVisualizationPropertyBufferShape(descriptor, diagnostics))
            {
                continue;
            }

            Entry& entry = m_Entries[descriptor.SourceKey];
            entry.LastSubmittedEpoch = m_UpdateEpoch;
            if (entry.DirtyStamp > 0u &&
                descriptor.DirtyStamp > 0u &&
                descriptor.DirtyStamp < entry.DirtyStamp)
            {
                ++diagnostics.StaleDirtyStampCount;
                diagnostics.HasErrors = true;
                continue;
            }

            // An unchanged buffer is reused without reading its payload, so a
            // steady frame costs O(1) per submitted buffer.
            if (Reusable(entry, descriptor))
            {
                ++diagnostics.AcceptedBufferCount;
                ++diagnostics.ReusedBufferCount;
                m_LastAddresses.push_back(
                    MakeAddress(descriptor.SourceKey, entry));
                continue;
            }

            if (!ValidateVisualizationPropertyBufferPayload(descriptor, diagnostics))
            {
                continue;
            }

            if (!m_Device->IsOperational())
            {
                ++diagnostics.UploadDeferralCount;
                diagnostics.HasErrors = true;
                continue;
            }

            // Frames still in flight may read the previous contents through
            // its address, so changed contents always go to a new buffer. The
            // superseded lease is released here and destroyed by the device's
            // per-frame deferred deletion once that frame's work completes.
            const std::uint64_t requestedBytes =
                static_cast<std::uint64_t>(descriptor.Bytes.size());
            const bool replacing = entry.Lease.has_value();
            entry.Lease.reset();
            entry.CapacityBytes = 0u;
            entry.BufferBDA = 0u;
            entry.DirtyStamp = 0u;

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
            if (replacing)
            {
                ++diagnostics.ReplacedBufferCount;
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
            entry.SourceLayoutStamp = descriptor.SourceLayoutStamp;
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

        // Producers resubmit every live input each frame; anything absent is
        // no longer referenced by the newest snapshot. Older snapshots that
        // are still rendering keep valid addresses because lease destruction
        // is deferred by the device until the current frame completes.
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
        {
            if (it->second.LastSubmittedEpoch == m_UpdateEpoch)
            {
                ++it;
                continue;
            }
            if (it->second.Lease.has_value())
            {
                ++diagnostics.EvictedBufferCount;
            }
            it = m_Entries.erase(it);
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
