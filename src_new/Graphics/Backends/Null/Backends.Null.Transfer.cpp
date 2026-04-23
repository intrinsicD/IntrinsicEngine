module;

#include <atomic>
#include <memory>
#include <span>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Telemetry;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace Extrinsic::Backends::Null
{
    class NullTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      const void*,
                                                      std::uint64_t,
                                                      std::uint64_t) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullTransferQueue::UploadBufferRaw", Extrinsic::Core::Telemetry::HashString("NullTransferQueue::UploadBufferRaw")};
            return RHI::TransferToken{++m_Next};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      std::span<const std::byte>,
                                                      std::uint64_t) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullTransferQueue::UploadBufferSpan", Extrinsic::Core::Telemetry::HashString("NullTransferQueue::UploadBufferSpan")};
            return RHI::TransferToken{++m_Next};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                       const void*,
                                                       std::uint64_t,
                                                       std::uint32_t,
                                                       std::uint32_t) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullTransferQueue::UploadTexture", Extrinsic::Core::Telemetry::HashString("NullTransferQueue::UploadTexture")};
            return RHI::TransferToken{++m_Next};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override
        {
            return token.Value <= m_Next.load(std::memory_order_relaxed);
        }

        void CollectCompleted() override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullTransferQueue::CollectCompleted", Extrinsic::Core::Telemetry::HashString("NullTransferQueue::CollectCompleted")};
        }

    private:
        std::atomic<std::uint64_t> m_Next{0};
    };

    std::unique_ptr<RHI::ITransferQueue> CreateNullTransferQueue()
    {
        return std::make_unique<NullTransferQueue>();
    }
}
