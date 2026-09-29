// GPU copies of CPU properties for GPU methods (ADR 0030, GRAPHICS-154). A canonical slot holds
// one property of one element domain, byte for byte, for one CPU revision: it is uploaded once
// when a method first needs that revision and shared by every later user while the revision
// holds. A new revision gets a new buffer; the old one is released through the device's
// frame-deferred destruction, which also covers immediate submits (they precede later frames
// on the same queue). Render buffers are never a source: data flows CPU -> residency -> render.
// Keys are ECS-blind; runtime binds entities and properties to them.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
export module Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; } }

export namespace Extrinsic::Graphics
{
    struct GpuPropertyKey
    {
        std::uint64_t Scope{};   // e.g. the world
        std::uint64_t Owner{};   // e.g. the stable entity id
        std::uint32_t Domain{};  // element domain
        std::uint32_t ValueKind{};
        std::string Name{};
        [[nodiscard]] bool operator==(const GpuPropertyKey&) const = default;
    };

    // A resident canonical slot. The lease keeps the slot from being released while held; the
    // buffer stays valid (and unwritten) as long as the lease lives.
    struct GpuPropertyView
    {
        RHI::BufferHandle Buffer{};
        std::uint64_t Address{};
        std::uint64_t Bytes{};
        std::uint64_t Revision{};
        std::uint32_t ElementBytes{};
        std::uint32_t Count{};
        std::shared_ptr<const void> Lease{};
        [[nodiscard]] bool Valid() const noexcept { return Buffer.IsValid() && Count > 0u; }
    };

    struct GpuPropertyResidencyStats
    {
        std::uint64_t Uploads{}, UploadBytes{}, Hits{}, Releases{}, ResidentBytes{};
        std::uint32_t Slots{};
    };

    class GpuPropertyResidency
    {
    public:
        explicit GpuPropertyResidency(RHI::IDevice& device);
        ~GpuPropertyResidency();
        GpuPropertyResidency(const GpuPropertyResidency&) = delete;
        GpuPropertyResidency& operator=(const GpuPropertyResidency&) = delete;

        // The slot for `key` at `revision`: a hit when it is resident, otherwise `fill` writes
        // count x elementBytes bytes into a staging span that is uploaded once (a previous
        // revision's slot is released). Empty on a non-operational device, an allocation
        // failure or a zero size. Device-owner thread only.
        [[nodiscard]] std::optional<GpuPropertyView> AcquireInput(
            const GpuPropertyKey& key, std::uint64_t revision, std::uint32_t elementBytes, std::uint32_t count,
            const std::function<void(std::span<std::byte>)>& fill);
        // Releases unleased slots whose key `keep` rejects (e.g. entities that no longer exist).
        void Prune(const std::function<bool(const GpuPropertyKey&)>& keep);
        // Releases every slot (device shutdown); outstanding views keep their handles but the
        // buffers are gone once the device retires them.
        void Clear();
        [[nodiscard]] GpuPropertyResidencyStats Stats() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
