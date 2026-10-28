// Uploads vector-field draw records and isoline packets into retained
// per-frame-slot GPU buffers.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.VisualizationOverlayUploadHelper;

import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; } }

export namespace Extrinsic::Graphics
{
    // One record per renderable vector-field packet, read by
    // `visualization_vector_field.vert` through the record buffer address. The rows
    // are declared once in `assets/shaders/common/vector_field_draw_record.glsl`
    // (GRAPHICS-165), which also gives the shader its struct and the upload helper its
    // fill; every record is filled from its packet.
    struct VisualizationVectorFieldDrawRecord
    {
        // The row file's GLSL type names.
        using mat4 = glm::mat4;
        using uint = std::uint32_t;
        using uint64_t = std::uint64_t;
#define VF_RECORD_PACKET(type, name) type name{};
#define VF_RECORD_STYLE(type, name) type name{};
#define VF_RECORD_DERIVED(type, name) type name{};
#include "common/vector_field_draw_record.glsl"
#undef VF_RECORD_PACKET
#undef VF_RECORD_STYLE
#undef VF_RECORD_DERIVED
    };
    // Each row type has its GLSL scalar size and alignment, so C++'s layout (each member
    // aligned to its type, the struct to its largest member) is GLSL's scalar layout,
    // padding and array stride included, for any rows.
    static_assert(sizeof(VisualizationVectorFieldDrawRecord::mat4) == 64u &&
        alignof(VisualizationVectorFieldDrawRecord::mat4) == 4u);
    static_assert(sizeof(VisualizationVectorFieldDrawRecord::uint64_t) == 8u &&
        alignof(VisualizationVectorFieldDrawRecord::uint64_t) == 8u);
    static_assert(sizeof(VisualizationVectorFieldDrawRecord::uint) == 4u &&
        alignof(VisualizationVectorFieldDrawRecord::uint) == 4u);
    static_assert(sizeof(float) == 4u && alignof(float) == 4u);

    inline constexpr std::uint32_t kVisualizationVectorFieldNormalizeFlag = 1u;
    // The color buffer holds colormap scalars / RGBA colors.
    inline constexpr std::uint32_t kVisualizationVectorFieldColormapFlag = 2u;
    inline constexpr std::uint32_t kVisualizationVectorFieldRgbaFlag = 4u;
    // Arrow glyph: a six-vertex shaft quad followed by a three-vertex head.
    inline constexpr std::uint32_t kVisualizationVectorFieldGlyphVertexCount = 9u;

    struct VisualizationVectorFieldUploadResult
    {
        RHI::BufferHandle RecordBuffer{};
        std::uint64_t     RecordBufferBDA{0u};
        std::uint32_t     RecordCount{0u};
        std::uint32_t     PacketCount{0u};
        // Record index per submitted packet; kInvalidRecord for packets that
        // were not renderable (unresolved buffers or invalid style).
        std::vector<std::uint32_t> RecordIndexForPacket{};
        bool              Uploaded{false};
        bool              Overflow{false};

        static constexpr std::uint32_t kInvalidRecord = 0xFFFFFFFFu;
    };

    // Each iso value contributes a deterministic fixture segment, not a
    // scalar-field-derived polyline.
    struct VisualizationIsolineUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // Each lane retains one growing buffer per frame slot. Vector-field work is
    // one record per packet; glyphs are expanded on the GPU from the source
    // buffer addresses. Isolines still upload fixture positions. Device and
    // manager must outlive the helper; the renderer resets it before manager
    // shutdown.
    class VisualizationOverlayUploadHelper
    {
    public:
        VisualizationOverlayUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~VisualizationOverlayUploadHelper();

        void BeginFrame(std::uint32_t frameIndex,
                        std::uint32_t framesInFlight);

        [[nodiscard]] VisualizationVectorFieldUploadResult UploadVectorFields(
            std::span<const VectorFieldOverlayPacket> vectorFields);

        [[nodiscard]] VisualizationIsolineUploadResult UploadIsolines(
            std::span<const IsolineOverlayPacket> isolines);

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept
        {
            return m_BufferAllocationCount;
        }

    private:
        RHI::IDevice*       m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};

        struct UploadBufferSlot
        {
            std::optional<RHI::BufferManager::BufferLease> Buffer{};
            std::uint64_t CapacityBytes{0u};
        };

        void EnsureFrameSlots(std::uint32_t framesInFlight);

        std::vector<UploadBufferSlot> m_VectorFieldRecordBufferSlots{};

        // GRAPHICS-078 Slice C — independent per-lane buffer lease for
        // the isoline lane. Grows independently of the vector-field
        // lane and is reset before the `BufferManager` in the
        // renderer's `Shutdown()` (via `m_VisualizationOverlayUploadHelper.reset()`
        // before `m_BufferManager.reset()`).
        std::vector<UploadBufferSlot> m_IsolineVertexBufferSlots{};

        std::uint32_t  m_ActiveSlot{0u};
        std::uint64_t  m_BufferAllocationCount{0u};
    };
}
