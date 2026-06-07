module;

#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.CameraSnapshots;

import Extrinsic.Core.Geometry2D;

namespace Extrinsic::Graphics
{
    export enum class FrustumPlaneIndex : std::uint32_t
    {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5,
    };

    export struct FrustumPlaneSnapshot
    {
        glm::vec4 Equation{0.f};
        bool Valid{false};
    };

    export struct CameraViewInput
    {
        glm::mat4 View{1.f};
        glm::mat4 Projection{1.f};
        glm::vec3 Position{0.f};
        glm::vec3 Forward{0.f, 0.f, -1.f};
        glm::vec3 Up{0.f, 1.f, 0.f};
        float NearPlane{0.1f};
        float FarPlane{1000.f};
        bool Valid{false};
        bool ExplicitCameraTransition{false};
    };

    export struct PickPixelRequest
    {
        std::uint32_t X{0u};
        std::uint32_t Y{0u};
        bool Pending{false};
        // RUNTIME-089: runtime selection correlation token. The runtime
        // SelectionController assigns each drained pick a unique monotonic
        // Sequence; it is threaded here -> RenderWorld::PickRequest -> the GPU
        // picking slot -> PickReadbackResult so the readback resolves the exact
        // in-flight request even when several picks are in flight and the
        // renderer publishes completed slots out of issue order. 0 means the
        // pick is uncorrelated (no runtime controller wired).
        std::uint64_t Sequence{0u};
    };

    export struct CameraViewSnapshot
    {
        glm::mat4 View{1.f};
        glm::mat4 Projection{1.f};
        glm::mat4 ViewProjection{1.f};
        glm::mat4 InverseViewProjection{1.f};
        glm::vec3 Position{0.f};
        glm::vec3 Forward{0.f, 0.f, -1.f};
        glm::vec3 Up{0.f, 1.f, 0.f};
        float NearPlane{0.1f};
        float FarPlane{1000.f};
        FrustumPlaneSnapshot FrustumPlanes[6]{};
        glm::vec3 PickRayOrigin{0.f};
        glm::vec3 PickRayDirection{0.f, 0.f, -1.f};
        bool Valid{false};
        bool HasPickRay{false};
        bool ExplicitCameraTransition{false};
    };

    export inline constexpr std::uint32_t kTemporalJitterSequenceLength = 16u;

    export struct TemporalJitterSample
    {
        glm::vec2 PixelOffset{0.f};
        glm::vec2 NdcOffset{0.f};
        std::uint32_t SequenceIndex{0u};
        bool Enabled{false};
    };

    export struct TemporalCameraViewSnapshot
    {
        CameraViewSnapshot Camera{};
        glm::vec2 JitterOffset{0.f};
        std::uint32_t JitterSequenceIndex{0u};
        bool HasTemporalJitter{false};
    };

    export [[nodiscard]] TemporalJitterSample ComputeTemporalJitterSample(
        std::uint64_t renderedFrameIndex,
        Core::Extent2D viewport,
        bool noJitterNoHistory = false) noexcept;

    export [[nodiscard]] glm::mat4 ApplyTemporalJitterProjectionOverride(
        glm::mat4 projection,
        glm::vec2 ndcOffset) noexcept;

    export [[nodiscard]] CameraViewSnapshot BuildCameraViewSnapshot(
        const CameraViewInput& input,
        Core::Extent2D viewport,
        PickPixelRequest pick = {}) noexcept;

    export [[nodiscard]] TemporalCameraViewSnapshot BuildTemporalCameraViewSnapshot(
        const CameraViewInput& input,
        Core::Extent2D viewport,
        PickPixelRequest pick,
        std::uint64_t renderedFrameIndex,
        bool enableTemporalJitter,
        bool noJitterNoHistory = false) noexcept;
}
