module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.GeometryResidency;

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

export namespace Extrinsic::Graphics
{
    // Graphics-only stable identity for one retained geometry allocation.
    // Runtime chooses the namespace/identity/lane values; graphics deliberately
    // does not know about ECS entities or topology domains.
    struct GeometryResidencyKey
    {
        std::uint64_t Namespace = 0u;
        std::uint64_t Identity = 0u;
        std::uint32_t Lane = 0u;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Namespace != 0u && Identity != 0u;
        }

        [[nodiscard]] constexpr bool operator==(
            const GeometryResidencyKey&) const noexcept = default;
    };

    struct GeometryResidencyKeyHash
    {
        [[nodiscard]] std::size_t operator()(
            const GeometryResidencyKey& key) const noexcept;
    };

    enum class GeometryUploadUpdateClass : std::uint8_t
    {
        FullReplacement,
        PartialPreferred,
    };

    struct GeometryUploadFormats
    {
        RHI::Format Position = RHI::Format::RGB32_FLOAT;
        RHI::Format Texcoord = RHI::Format::RG32_FLOAT;
        RHI::Format Normal = RHI::Format::RGB32_FLOAT;
        RHI::Format Color = RHI::Format::R32_UINT;
        RHI::Format SurfaceIndex = RHI::Format::R32_UINT;
        RHI::Format LineIndex = RHI::Format::R32_UINT;
    };

    // Owning copy submitted across the runtime -> graphics boundary. Its byte
    // vectors never borrow ECS/property/topology storage.
    struct GeometryUploadPlan
    {
        GeometryResidencyKey Key{};
        std::uint64_t Generation = 0u;
        std::vector<std::byte> PackedVertexBytes{};
        std::vector<std::byte> PositionBytes{};
        std::vector<std::byte> TexcoordBytes{};
        std::vector<std::byte> NormalBytes{};
        std::vector<std::uint32_t> PackedVertexColors{};
        std::vector<std::uint32_t> SurfaceIndices{};
        std::vector<std::uint32_t> LineIndices{};
        std::uint32_t VertexCount = 0u;
        RHI::GpuBounds LocalBounds{};
        std::string DebugName{};
        GeometryUploadFormats Formats{};
        GeometryUploadUpdateClass UpdateClass =
            GeometryUploadUpdateClass::FullReplacement;
        GpuWorld::GeometryChannelUpdateMask UpdateChannels{};
        GpuWorld::GeometryStorageHint StorageHint =
            GpuWorld::GeometryStorageHint::DynamicSoA;

        [[nodiscard]] GpuWorld::GeometryUploadDesc UploadDesc() const noexcept;
    };

    [[nodiscard]] GeometryUploadPlan MakeGeometryUploadPlan(
        GeometryResidencyKey key,
        std::uint64_t generation,
        const GpuWorld::GeometryUploadDesc& upload,
        GeometryUploadUpdateClass updateClass =
            GeometryUploadUpdateClass::FullReplacement,
        GpuWorld::GeometryChannelUpdateMask updateChannels = {},
        GpuWorld::GeometryStorageHint storageHint =
            GpuWorld::GeometryStorageHint::DynamicSoA);

    enum class GeometryUploadPlanStatus : std::uint8_t
    {
        Valid,
        InvalidKey,
        MissingGeneration,
        EmptyGeometry,
        MissingPositions,
        InvalidPositionBytes,
        InvalidTexcoordBytes,
        InvalidNormalBytes,
        InvalidColorCount,
        InvalidPackedVertexBytes,
        InvalidIndex,
        UnsupportedFormat,
        InvalidPartialUpdate,
    };

    struct GeometryUploadPlanValidation
    {
        GeometryUploadPlanStatus Status = GeometryUploadPlanStatus::Valid;
        std::string_view Diagnostic{"valid"};

        [[nodiscard]] constexpr bool Valid() const noexcept
        {
            return Status == GeometryUploadPlanStatus::Valid;
        }
    };

    [[nodiscard]] GeometryUploadPlanValidation ValidateGeometryUploadPlan(
        const GeometryUploadPlan& plan) noexcept;

    enum class GeometryResidencyStatus : std::uint8_t
    {
        Uploaded,
        Reused,
        PartiallyUpdated,
        FullyReuploaded,
        InvalidPlan,
        StalePlan,
        UploadFailed,
        RefCountSaturated,
    };

    struct GeometryResidencyResult
    {
        GeometryResidencyStatus Status = GeometryResidencyStatus::InvalidPlan;
        GpuGeometryHandle Handle{};
        GpuGeometryHandle RetiringHandle{};
        GeometryUploadPlanValidation Validation{};
        GpuWorld::GeometryChannelUpdateStatus ChannelUpdateStatus =
            GpuWorld::GeometryChannelUpdateStatus::NoChannels;
        GpuWorld::GeometryStoragePlan StoragePlan{};
        std::string_view Diagnostic{"invalid plan"};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    struct GeometryResidencyStats
    {
        std::uint64_t Uploads = 0u;
        std::uint64_t ReuseHits = 0u;
        std::uint64_t PartialUpdates = 0u;
        std::uint64_t FullReuploads = 0u;
        std::uint64_t Releases = 0u;
        std::uint64_t FailedUploads = 0u;
        std::uint64_t InvalidPlans = 0u;
        std::uint64_t StalePlans = 0u;
        std::uint64_t RefCountSaturated = 0u;
        std::uint64_t FreeRetires = 0u;
        std::uint64_t RetireCancellations = 0u;
    };

    struct GeometryResidencyView
    {
        GeometryResidencyKey Key{};
        GpuGeometryHandle Handle{};
        std::uint64_t Generation = 0u;
        std::uint32_t RefCount = 0u;
        GpuWorld::GeometryStoragePlan StoragePlan{};
        bool PendingRetire = false;
    };

    // One concrete lifecycle owner composed with the existing GpuWorld. It
    // does not allocate buffers itself and is intentionally not an interface,
    // service, factory, or registry.
    class GeometryResidencyCoordinator
    {
    public:
        explicit GeometryResidencyCoordinator(GpuWorld& world);
        ~GeometryResidencyCoordinator();

        GeometryResidencyCoordinator(const GeometryResidencyCoordinator&) = delete;
        GeometryResidencyCoordinator& operator=(const GeometryResidencyCoordinator&) = delete;

        // Single-owner reconciliation: a same-generation submission reuses the
        // entry without changing its reference count.
        [[nodiscard]] GeometryResidencyResult Reconcile(
            const GeometryUploadPlan& plan);

        // Shared acquisition: a same-generation submission adds one owner.
        [[nodiscard]] GeometryResidencyResult Acquire(
            const GeometryUploadPlan& plan);

        // Drop one owner. The last release enters frame-safe retirement.
        [[nodiscard]] bool Release(GeometryResidencyKey key);
        void Tick(std::uint64_t currentFrame, std::uint32_t framesInFlight);
        void Shutdown();

        [[nodiscard]] std::optional<GeometryResidencyView> Find(
            GeometryResidencyKey key) const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t PendingRetireCount() const noexcept;
        [[nodiscard]] const GeometryResidencyStats& Stats() const noexcept;

        // Explicit test seam for the otherwise unreachable saturation branch.
        [[nodiscard]] bool PrimeRefCountForTest(
            GeometryResidencyKey key,
            std::uint32_t refCount) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
