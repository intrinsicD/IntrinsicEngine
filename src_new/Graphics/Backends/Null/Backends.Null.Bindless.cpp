module;

#include <cassert>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Telemetry;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Backends::Null
{
    class NullBindlessHeap final : public RHI::IBindlessHeap
    {
    public:
        [[nodiscard]] RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullBindlessHeap::AllocateTextureSlot", Extrinsic::Core::Telemetry::HashString("NullBindlessHeap::AllocateTextureSlot")};
            std::scoped_lock lock{m_Mutex};

            RHI::BindlessIndex slot = RHI::kInvalidBindlessIndex;
            if (!m_FreeSlots.empty())
            {
                slot = m_FreeSlots.back();
                m_FreeSlots.pop_back();
            }
            else
            {
                if (m_NextSlot >= kCapacity)
                    return RHI::kInvalidBindlessIndex;
                slot = m_NextSlot++;
            }

            m_PendingOps.push_back(PendingOp{PendingOpType::Allocate, slot, {}, {}});
            return slot;
        }

        void UpdateTextureSlot(RHI::BindlessIndex slot, RHI::TextureHandle texture, RHI::SamplerHandle sampler) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullBindlessHeap::UpdateTextureSlot", Extrinsic::Core::Telemetry::HashString("NullBindlessHeap::UpdateTextureSlot")};
            std::scoped_lock lock{m_Mutex};
            if (slot == RHI::kInvalidBindlessIndex || slot >= m_NextSlot)
                return;
            m_PendingOps.push_back(PendingOp{PendingOpType::Update, slot, texture, sampler});
        }

        void FreeSlot(RHI::BindlessIndex slot) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullBindlessHeap::FreeSlot", Extrinsic::Core::Telemetry::HashString("NullBindlessHeap::FreeSlot")};
            std::scoped_lock lock{m_Mutex};
            if (slot == RHI::kInvalidBindlessIndex || slot >= m_NextSlot)
                return;
            m_PendingOps.push_back(PendingOp{PendingOpType::Free, slot, {}, {}});
        }

        void FlushPending() override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullBindlessHeap::FlushPending", Extrinsic::Core::Telemetry::HashString("NullBindlessHeap::FlushPending")};
            std::scoped_lock lock{m_Mutex};
            for (const PendingOp& op : m_PendingOps)
            {
                switch (op.Type)
                {
                case PendingOpType::Allocate:
                    if (const auto [_, inserted] = m_AllocatedSlots.emplace(op.Slot); inserted)
                        ++m_AllocatedSlotCount;
                    break;
                case PendingOpType::Update:
                    if (m_AllocatedSlots.contains(op.Slot))
                        m_Bindings[op.Slot] = SlotBinding{op.Texture, op.Sampler};
                    break;
                case PendingOpType::Free:
                    if (m_AllocatedSlots.erase(op.Slot) > 0)
                    {
                        m_Bindings.erase(op.Slot);
                        m_FreeSlots.push_back(op.Slot);
                        assert(m_AllocatedSlotCount > 0 && "Null bindless allocated count underflow");
                        --m_AllocatedSlotCount;
                    }
                    break;
                }
            }
            m_PendingOps.clear();

            assert(m_AllocatedSlotCount == m_AllocatedSlots.size() &&
                   "Null bindless accounting mismatch after FlushPending");
        }

        [[nodiscard]] std::uint32_t GetCapacity() const override { return kCapacity; }
        [[nodiscard]] std::uint32_t GetAllocatedSlotCount() const override { return m_AllocatedSlotCount; }

    private:
        static constexpr std::uint32_t kCapacity = 65536;

        enum class PendingOpType : std::uint8_t { Allocate, Update, Free };

        struct SlotBinding
        {
            RHI::TextureHandle Texture{};
            RHI::SamplerHandle Sampler{};
        };

        struct PendingOp
        {
            PendingOpType Type{};
            RHI::BindlessIndex Slot{};
            RHI::TextureHandle Texture{};
            RHI::SamplerHandle Sampler{};
        };

        mutable std::mutex m_Mutex;
        std::uint32_t m_NextSlot = 1;
        std::uint32_t m_AllocatedSlotCount = 0;
        std::vector<RHI::BindlessIndex> m_FreeSlots;
        std::vector<PendingOp> m_PendingOps;
        std::unordered_set<RHI::BindlessIndex> m_AllocatedSlots;
        std::unordered_map<RHI::BindlessIndex, SlotBinding> m_Bindings;
    };

    std::unique_ptr<RHI::IBindlessHeap> CreateNullBindlessHeap()
    {
        return std::make_unique<NullBindlessHeap>();
    }
}
