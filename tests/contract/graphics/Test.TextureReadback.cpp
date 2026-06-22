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
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace
{
    namespace RHI = Extrinsic::RHI;

    [[nodiscard]] RHI::TextureDesc MakeReadbackTextureDesc(RHI::Format fmt = RHI::Format::RGBA8_UNORM)
    {
        return RHI::TextureDesc{
            .Width = 2u,
            .Height = 2u,
            .DepthOrArrayLayers = 2u,
            .MipLevels = 2u,
            .Fmt = fmt,
            .Dimension = RHI::TextureDimension::Tex2D,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferSrc,
            .InitialLayout = RHI::TextureLayout::TransferSrc,
            .DebugName = "MockTextureReadback.Texture",
        };
    }

    class MockTextureReadbackTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::TextureHandle AddTexture(const RHI::TextureDesc& desc,
                                                    std::vector<std::byte> bytes)
        {
            const auto index = static_cast<std::uint32_t>(m_Textures.size());
            m_Textures.push_back(TextureRecord{
                .Desc = desc,
                .Bytes = std::move(bytes),
            });
            return RHI::TextureHandle{index, 1u};
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

        [[nodiscard]] RHI::ReadbackToken DownloadTexture(RHI::TextureHandle src,
                                                         RHI::TextureLayout srcLayout,
                                                         std::uint32_t mipLevel,
                                                         std::uint32_t arrayLayer,
                                                         RHI::ReadbackSink sink) override
        {
            const TextureRecord* record = Lookup(src);
            if (record == nullptr ||
                srcLayout != RHI::TextureLayout::TransferSrc ||
                !RHI::IsUploadableFormat(record->Desc.Fmt) ||
                RHI::IsDepthStencilFormat(record->Desc.Fmt) ||
                !HasTextureUsage(record->Desc.Usage, RHI::TextureUsage::TransferSrc) ||
                record->Desc.Dimension == RHI::TextureDimension::Tex3D)
            {
                ++m_Diagnostics.DownloadsDropped;
                return {};
            }

            auto layoutOr = RHI::ComputeFullChainUploadLayout(record->Desc);
            if (!layoutOr.has_value())
            {
                ++m_Diagnostics.DownloadsDropped;
                return {};
            }

            const RHI::TextureUploadSubresource* selected = nullptr;
            for (const RHI::TextureUploadSubresource& sub : layoutOr->Subresources)
            {
                if (sub.MipLevel == mipLevel && sub.ArrayLayer == arrayLayer)
                {
                    selected = &sub;
                    break;
                }
            }
            if (selected == nullptr ||
                selected->OffsetBytes > record->Bytes.size() ||
                selected->SizeBytes > record->Bytes.size() - selected->OffsetBytes ||
                !sink.IsValidForSize(selected->SizeBytes))
            {
                ++m_Diagnostics.DownloadsDropped;
                return {};
            }

            std::vector<std::byte> bytes(static_cast<std::size_t>(selected->SizeBytes));
            std::copy_n(record->Bytes.begin() + static_cast<std::ptrdiff_t>(selected->OffsetBytes),
                        bytes.size(),
                        bytes.begin());

            const RHI::ReadbackToken token{++m_NextReadback};
            m_Pending.push_back(PendingReadback{
                .Token = token,
                .Sink = std::move(sink),
                .Bytes = std::move(bytes),
            });
            ++m_Diagnostics.DownloadsQueued;
            m_Diagnostics.ReadbackBytesStaged += selected->SizeBytes;
            m_StagedBytes += static_cast<std::size_t>(selected->SizeBytes);
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
        struct TextureRecord
        {
            RHI::TextureDesc Desc{};
            std::vector<std::byte> Bytes{};
        };

        struct PendingReadback
        {
            RHI::ReadbackToken Token{};
            RHI::ReadbackSink Sink{};
            std::vector<std::byte> Bytes{};
        };

        [[nodiscard]] static bool HasTextureUsage(RHI::TextureUsage flags,
                                                  RHI::TextureUsage bit) noexcept
        {
            return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(bit)) != 0u;
        }

        [[nodiscard]] const TextureRecord* Lookup(RHI::TextureHandle handle) const noexcept
        {
            if (!handle.IsValid() || handle.Index >= m_Textures.size())
                return nullptr;
            return &m_Textures[handle.Index];
        }

        std::vector<TextureRecord> m_Textures{};
        std::deque<PendingReadback> m_Pending{};
        std::uint64_t m_NextReadback = 0;
        std::uint64_t m_CompletedReadback = 0;
        std::size_t m_StagedBytes = 0;
        RHI::TransferQueueDiagnostics m_Diagnostics{};
    };
}

TEST(TextureReadback, NullBackendRejectsDownloadsFailClosed)
{
    std::unique_ptr<RHI::IDevice> device = Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    std::array<std::byte, 4> destination{};
    RHI::ITransferQueue& queue = device->GetTransferQueue();

    const RHI::TransferQueueDiagnostics before = queue.GetDiagnostics();
    const RHI::ReadbackToken token =
        queue.DownloadTexture(RHI::TextureHandle{0u, 1u},
                              RHI::TextureLayout::TransferSrc,
                              0u,
                              0u,
                              RHI::ReadbackSink::CopyTo(std::span<std::byte>{destination}));

    EXPECT_FALSE(token.IsValid());
    EXPECT_TRUE(queue.IsComplete(token));
    const RHI::TransferQueueDiagnostics after = queue.GetDiagnostics();
    EXPECT_EQ(after.DownloadsQueued, before.DownloadsQueued);
    EXPECT_EQ(after.DownloadsCompleted, before.DownloadsCompleted);
    EXPECT_EQ(after.DownloadsDropped, before.DownloadsDropped + 1u);
}

TEST(TextureReadback, MockQueueDeliversSubresourceOnCollectCompleted)
{
    MockTextureReadbackTransferQueue queue;
    const RHI::TextureDesc desc = MakeReadbackTextureDesc();
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());

    std::vector<std::byte> textureBytes(static_cast<std::size_t>(layoutOr->TotalBytes));
    for (std::size_t i = 0; i < textureBytes.size(); ++i)
        textureBytes[i] = static_cast<std::byte>(0x20u + static_cast<unsigned>(i));

    const RHI::TextureHandle texture = queue.AddTexture(desc, textureBytes);
    std::array<std::byte, 4> destination{};
    std::vector<std::byte> callbackBytes{};
    std::uint32_t callbackCount = 0u;
    RHI::ReadbackSink sink{
        .Destination = std::span<std::byte>{destination},
        .Callback = [&](std::span<const std::byte> bytes)
        {
            ++callbackCount;
            callbackBytes.assign(bytes.begin(), bytes.end());
        },
    };

    const RHI::ReadbackToken token =
        queue.DownloadTexture(texture,
                              RHI::TextureLayout::TransferSrc,
                              1u,
                              1u,
                              std::move(sink));

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
    const std::array expected{std::byte{0x44}, std::byte{0x45}, std::byte{0x46}, std::byte{0x47}};
    EXPECT_EQ(destination, expected);
    EXPECT_EQ(callbackBytes, std::vector<std::byte>(expected.begin(), expected.end()));

    queue.CollectCompleted();
    EXPECT_EQ(callbackCount, 1u);
    EXPECT_EQ(queue.GetDiagnostics().DownloadsCompleted, 1u);
}

TEST(TextureReadback, MockQueueDropsBadFormatsAndSubresources)
{
    MockTextureReadbackTransferQueue queue;

    const RHI::TextureDesc colorDesc = MakeReadbackTextureDesc();
    auto colorLayoutOr = RHI::ComputeFullChainUploadLayout(colorDesc);
    ASSERT_TRUE(colorLayoutOr.has_value());
    const RHI::TextureHandle colorTexture =
        queue.AddTexture(colorDesc, std::vector<std::byte>(static_cast<std::size_t>(colorLayoutOr->TotalBytes)));

    std::array<std::byte, 4> destination{};
    const RHI::ReadbackToken badMip =
        queue.DownloadTexture(colorTexture,
                              RHI::TextureLayout::TransferSrc,
                              colorDesc.MipLevels,
                              0u,
                              RHI::ReadbackSink::CopyTo(std::span<std::byte>{destination}));
    EXPECT_FALSE(badMip.IsValid());

    RHI::TextureDesc depthDesc = MakeReadbackTextureDesc(RHI::Format::D32_FLOAT);
    depthDesc.Usage = RHI::TextureUsage::DepthTarget | RHI::TextureUsage::TransferSrc;
    const RHI::TextureHandle depthTexture = queue.AddTexture(depthDesc, {});
    const RHI::ReadbackToken badFormat =
        queue.DownloadTexture(depthTexture,
                              RHI::TextureLayout::TransferSrc,
                              0u,
                              0u,
                              RHI::ReadbackSink::CopyTo(std::span<std::byte>{destination}));
    EXPECT_FALSE(badFormat.IsValid());

    const RHI::ReadbackToken badLayout =
        queue.DownloadTexture(colorTexture,
                              RHI::TextureLayout::ShaderReadOnly,
                              0u,
                              0u,
                              RHI::ReadbackSink::CopyTo(std::span<std::byte>{destination}));
    EXPECT_FALSE(badLayout.IsValid());

    const RHI::TransferQueueDiagnostics diagnostics = queue.GetDiagnostics();
    EXPECT_EQ(diagnostics.DownloadsQueued, 0u);
    EXPECT_EQ(diagnostics.DownloadsCompleted, 0u);
    EXPECT_EQ(diagnostics.DownloadsDropped, 3u);
    EXPECT_EQ(diagnostics.ReadbackBytesStaged, 0u);
}
