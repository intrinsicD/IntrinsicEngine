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

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                std::span<const std::byte>) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullTransferQueue::UploadTextureFullChain", Extrinsic::Core::Telemetry::HashString("NullTransferQueue::UploadTextureFullChain")};
            return {};
        }

        [[nodiscard]] RHI::ReadbackToken DownloadBuffer(RHI::BufferHandle,
                                                        std::uint64_t,
                                                        std::uint64_t,
                                                        RHI::ReadbackSink) override
        {
            m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
            return {};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override
        {
            return token.Value <= m_Next.load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool IsComplete(RHI::ReadbackToken token) const override
        {
            return !token.IsValid();
        }

        void CollectCompleted() override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullTransferQueue::CollectCompleted", Extrinsic::Core::Telemetry::HashString("NullTransferQueue::CollectCompleted")};
        }

        [[nodiscard]] RHI::TransferQueueDiagnostics GetDiagnostics() const noexcept override
        {
            return RHI::TransferQueueDiagnostics{
                .DownloadsDropped = m_DownloadsDropped.load(std::memory_order_relaxed),
            };
        }

    private:
        std::atomic<std::uint64_t> m_Next{0};
        std::atomic<std::uint64_t> m_DownloadsDropped{0};
    };

    std::unique_ptr<RHI::ITransferQueue> CreateNullTransferQueue()
    {
        return std::make_unique<NullTransferQueue>();
    }
}
