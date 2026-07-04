module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

export module Extrinsic.Graphics.ImGuiUploadHelper;

import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

// GRAPHICS-079 Slice C / GRAPHICS-110 — per-frame host-visible upload helper
// for the canonical ImGui overlay pass. The helper mirrors the renderer-owned
// transient-debug / visualization-overlay helpers: it owns growing vertex/index
// upload storage per frame-in-flight slot, copies submitted
// `ImGuiOverlayFrame` payloads through `IDevice::WriteBuffer(...)`, and
// returns per-draw-list offsets so `Pass.ImGui` can record deterministic
// `BindIndexBuffer + PushConstants + DrawIndexed` blocks.

export namespace Extrinsic::Graphics
{
    struct ImGuiDrawCommandUploadResult
    {
        std::uint32_t IndexOffset{0u};
        std::uint32_t VertexOffset{0u};
        std::uint32_t IndexCount{0u};
        RHI::BindlessIndex TextureBindlessIndex{RHI::kInvalidBindlessIndex};
        bool UsesUserTexture{false};
    };

    struct ImGuiDrawListUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t VertexBufferBDA{0u};
        std::uint64_t IndexOffsetBytes{0u};
        std::uint32_t FirstVertex{0u};
        std::uint32_t VertexCount{0u};
        std::uint32_t IndexCount{0u};
        std::vector<ImGuiDrawCommandUploadResult> Commands{};
        bool Uploaded{false};
        bool Overflow{false};
    };

    struct ImGuiUploadResult
    {
        std::vector<ImGuiDrawListUploadResult> DrawLists{};
        std::uint32_t DrawListCount{0u};
        std::uint32_t DrawCommandCount{0u};
        std::uint32_t CommandUploadListBuilds{0u};
        std::uint64_t VertexUploadBytes{0u};
        std::uint64_t IndexUploadBytes{0u};
        std::uint64_t TotalUploadBytes{0u};
        bool Uploaded{false};
        bool Overflow{false};
    };

    class IImGuiUploadHelper
    {
    public:
        virtual ~IImGuiUploadHelper() = default;

        IImGuiUploadHelper(const IImGuiUploadHelper&) = delete;
        IImGuiUploadHelper& operator=(const IImGuiUploadHelper&) = delete;

        virtual void BeginFrame(std::uint32_t frameIndex,
                                std::uint32_t framesInFlight) = 0;

        [[nodiscard]] virtual ImGuiUploadResult UploadFrame(
            const ImGuiOverlayFrame& frame) = 0;

        [[nodiscard]] virtual std::uint64_t GetBufferAllocationCount() const noexcept = 0;

    protected:
        IImGuiUploadHelper() = default;
    };

    class ImGuiUploadHelper final : public IImGuiUploadHelper
    {
    public:
        ImGuiUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~ImGuiUploadHelper() override;

        void BeginFrame(std::uint32_t frameIndex,
                        std::uint32_t framesInFlight) override;

        [[nodiscard]] ImGuiUploadResult UploadFrame(
            const ImGuiOverlayFrame& frame) override;

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept override
        {
            return m_BufferAllocationCount;
        }

    private:
        RHI::IDevice* m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};

        struct UploadBufferSlot
        {
            std::optional<RHI::BufferManager::BufferLease> Buffer{};
            std::uint64_t CapacityBytes{0u};
        };

        void EnsureFrameSlots(std::uint32_t framesInFlight);

        std::vector<UploadBufferSlot> m_VertexBufferSlots{};
        std::vector<UploadBufferSlot> m_IndexBufferSlots{};
        std::vector<ImGuiOverlayVertex> m_VertexScratch{};
        std::vector<std::uint32_t> m_IndexScratch{};
        std::uint32_t m_ActiveSlot{0u};
        std::uint64_t m_BufferAllocationCount{0u};
    };
}
