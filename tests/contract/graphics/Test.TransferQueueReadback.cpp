#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <utility>
#include <vector>

import Extrinsic.Backends.Null;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace
{
    namespace RHI = Extrinsic::RHI;

    class MockReadbackTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::BufferHandle AddBuffer(std::vector<std::byte> bytes,
                                                  RHI::BufferUsage usage = RHI::BufferUsage::TransferSrc)
        {
            const auto index = static_cast<std::uint32_t>(m_Buffers.size());
            m_Buffers.push_back(BufferRecord{
                .Desc = RHI::BufferDesc{
                    .SizeBytes = static_cast<std::uint64_t>(bytes.size()),
                    .Usage = usage,
                    .HostVisible = false,
                    .DebugName = "MockReadbackTransferQueue.Buffer",
                },
                .Bytes = std::move(bytes),
            });
            return RHI::BufferHandle{index, 1u};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                       const void*,
                                                       std::uint64_t,
                                                       std::uint64_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                       std::span<const std::byte>,
                                                       std::uint64_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                        const void*,
                                                        std::uint64_t,
                                                        std::uint32_t,
                                                        std::uint32_t) override
        {
            return {};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override
        {
            return !token.IsValid();
        }

        void CollectCompleted() override
        {
            while (!m_Pending.empty())
            {
                PendingReadback pending = std::move(m_Pending.front());
                m_Pending.pop_front();
                pending.Sink.Deliver(std::span<const std::byte>{pending.Bytes});
                m_CompletedReadback = std::max(m_CompletedReadback, pending.Token.Value);
                ++m_Diagnostics.DownloadsCompleted;
                m_StagedBytes -= pending.Bytes.size();
            }
        }

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                 std::span<const std::byte>) override
        {
            return {};
        }

        [[nodiscard]] RHI::ReadbackToken DownloadBuffer(RHI::BufferHandle src,
                                                         std::uint64_t size,
                                                         std::uint64_t offset,
                                                         RHI::ReadbackSink sink) override
        {
            const BufferRecord* record = Lookup(src);
            if (record == nullptr || !RHI::HasUsage(record->Desc.Usage, RHI::BufferUsage::TransferSrc))
            {
                ++m_Diagnostics.DownloadsDropped;
                return {};
            }

            if (!sink.IsValidForSize(size))
            {
                ++m_Diagnostics.DownloadsDropped;
                return {};
            }

            const auto range = RHI::ValidateBufferRange(record->Desc,
                                                        RHI::BufferRange{
                                                            .OffsetBytes = offset,
                                                            .SizeBytes = size,
                                                        });
            if (!range.has_value())
            {
                ++m_Diagnostics.DownloadsDropped;
                return {};
            }

            std::vector<std::byte> bytes(static_cast<std::size_t>(range->SizeBytes));
            std::copy_n(record->Bytes.begin() + static_cast<std::ptrdiff_t>(range->OffsetBytes),
                        bytes.size(),
                        bytes.begin());

            const RHI::ReadbackToken token{++m_NextReadback};
            m_Pending.push_back(PendingReadback{
                .Token = token,
                .Sink = std::move(sink),
                .Bytes = std::move(bytes),
            });
            ++m_Diagnostics.DownloadsQueued;
            m_Diagnostics.ReadbackBytesStaged += size;
            m_StagedBytes += static_cast<std::size_t>(size);
            m_Diagnostics.ReadbackRingHighWaterBytes =
                std::max<std::uint64_t>(m_Diagnostics.ReadbackRingHighWaterBytes,
                                        static_cast<std::uint64_t>(m_StagedBytes));
            return token;
        }

        [[nodiscard]] bool IsComplete(RHI::ReadbackToken token) const override
        {
            return !token.IsValid() || token.Value <= m_CompletedReadback;
        }

        [[nodiscard]] RHI::TransferQueueDiagnostics GetDiagnostics() const noexcept override
        {
            return m_Diagnostics;
        }

    private:
        struct BufferRecord
        {
            RHI::BufferDesc Desc{};
            std::vector<std::byte> Bytes{};
        };

        struct PendingReadback
        {
            RHI::ReadbackToken Token{};
            RHI::ReadbackSink Sink{};
            std::vector<std::byte> Bytes{};
        };

        [[nodiscard]] const BufferRecord* Lookup(RHI::BufferHandle handle) const noexcept
        {
            if (!handle.IsValid() || handle.Index >= m_Buffers.size())
                return nullptr;
            return &m_Buffers[handle.Index];
        }

        std::vector<BufferRecord> m_Buffers{};
        std::deque<PendingReadback> m_Pending{};
        std::uint64_t m_NextReadback = 0;
        std::uint64_t m_CompletedReadback = 0;
        std::size_t m_StagedBytes = 0;
        RHI::TransferQueueDiagnostics m_Diagnostics{};
    };
}

TEST(TransferQueueReadback, NullBackendRejectsDownloadsFailClosed)
{
    std::unique_ptr<RHI::IDevice> device = Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    std::array<std::byte, 4> destination{};
    RHI::ITransferQueue& queue = device->GetTransferQueue();

    const RHI::TransferQueueDiagnostics before = queue.GetDiagnostics();
    const RHI::ReadbackToken token = queue.DownloadBuffer(RHI::BufferHandle{0u, 1u},
                                                          destination.size(),
                                                          0u,
                                                          RHI::ReadbackSink::CopyTo(std::span<std::byte>{destination}));

    EXPECT_FALSE(token.IsValid());
    EXPECT_TRUE(queue.IsComplete(token));
    const RHI::TransferQueueDiagnostics after = queue.GetDiagnostics();
    EXPECT_EQ(after.DownloadsQueued, before.DownloadsQueued);
    EXPECT_EQ(after.DownloadsCompleted, before.DownloadsCompleted);
    EXPECT_EQ(after.DownloadsDropped, before.DownloadsDropped + 1u);
}

TEST(TransferQueueReadback, MockQueueDeliversReadbackOnCollectCompleted)
{
    MockReadbackTransferQueue queue;
    const RHI::BufferHandle buffer = queue.AddBuffer({
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
        std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80},
    });

    std::array<std::byte, 4> destination{};
    std::vector<std::byte> callbackBytes{};
    std::uint32_t callbackCount = 0;

    RHI::ReadbackSink sink{
        .Destination = std::span<std::byte>{destination},
        .Callback = [&](std::span<const std::byte> bytes)
        {
            ++callbackCount;
            callbackBytes.assign(bytes.begin(), bytes.end());
        },
    };

    const RHI::ReadbackToken token = queue.DownloadBuffer(buffer, destination.size(), 2u, std::move(sink));
    ASSERT_TRUE(token.IsValid());
    EXPECT_FALSE(queue.IsComplete(token));
    EXPECT_EQ(callbackCount, 0u);

    const RHI::TransferQueueDiagnostics queued = queue.GetDiagnostics();
    EXPECT_EQ(queued.DownloadsQueued, 1u);
    EXPECT_EQ(queued.DownloadsCompleted, 0u);
    EXPECT_EQ(queued.DownloadsDropped, 0u);
    EXPECT_EQ(queued.ReadbackBytesStaged, destination.size());
    EXPECT_EQ(queued.ReadbackRingHighWaterBytes, destination.size());

    queue.CollectCompleted();

    EXPECT_TRUE(queue.IsComplete(token));
    EXPECT_EQ(callbackCount, 1u);
    const std::array expected{std::byte{0x30}, std::byte{0x40}, std::byte{0x50}, std::byte{0x60}};
    EXPECT_EQ(destination, expected);
    EXPECT_EQ(callbackBytes, std::vector<std::byte>(expected.begin(), expected.end()));

    queue.CollectCompleted();
    EXPECT_EQ(callbackCount, 1u);
    EXPECT_EQ(queue.GetDiagnostics().DownloadsCompleted, 1u);
}

TEST(TransferQueueReadback, MockQueueDropsOutOfRangeRequests)
{
    MockReadbackTransferQueue queue;
    const RHI::BufferHandle buffer = queue.AddBuffer({
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
    });
    std::array<std::byte, 4> destination{};

    const RHI::ReadbackToken token = queue.DownloadBuffer(buffer,
                                                          destination.size(),
                                                          2u,
                                                          RHI::ReadbackSink::CopyTo(std::span<std::byte>{destination}));

    EXPECT_FALSE(token.IsValid());
    const RHI::TransferQueueDiagnostics diagnostics = queue.GetDiagnostics();
    EXPECT_EQ(diagnostics.DownloadsQueued, 0u);
    EXPECT_EQ(diagnostics.DownloadsCompleted, 0u);
    EXPECT_EQ(diagnostics.DownloadsDropped, 1u);
    EXPECT_EQ(diagnostics.ReadbackBytesStaged, 0u);
}
