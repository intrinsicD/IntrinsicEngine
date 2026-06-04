module;

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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
        bool ExplicitCameraTransition{false};
        bool Valid{false};
    };

    export struct PickPixelRequest
    {
        std::uint32_t X{0u};
        std::uint32_t Y{0u};
        bool Pending{false};
        // RUNTIME-089 — runtime selection correlation token. The runtime
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
        bool ExplicitCameraTransition{false};
        bool Valid{false};
        bool HasPickRay{false};
    };

    namespace Detail
    {
        [[nodiscard]] constexpr glm::vec4 Row(const glm::mat4& m, const int row) noexcept
        {
            return {m[0][row], m[1][row], m[2][row], m[3][row]};
        }

        [[nodiscard]] inline bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] inline bool IsFinite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] inline bool IsFinite(const glm::mat4& value) noexcept
        {
            for (int col = 0; col < 4; ++col)
            {
                if (!IsFinite(value[col]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] inline FrustumPlaneSnapshot NormalizePlane(const glm::vec4 equation) noexcept
        {
            const glm::vec3 normal{equation.x, equation.y, equation.z};
            const float len = glm::length(normal);
            if (!std::isfinite(len) || len <= 0.000001f)
                return {};
            return FrustumPlaneSnapshot{.Equation = equation / len, .Valid = true};
        }
    }

    export [[nodiscard]] inline CameraViewSnapshot BuildCameraViewSnapshot(
        const CameraViewInput& input,
        const Core::Extent2D viewport,
        const PickPixelRequest pick = {}) noexcept
    {
        CameraViewSnapshot snapshot{};
        snapshot.View = input.View;
        snapshot.Projection = input.Projection;
        snapshot.ViewProjection = input.Projection * input.View;
        snapshot.Position = input.Position;
        snapshot.Forward = input.Forward;
        snapshot.Up = input.Up;
        snapshot.NearPlane = input.NearPlane;
        snapshot.FarPlane = input.FarPlane;
        snapshot.ExplicitCameraTransition = input.ExplicitCameraTransition;

        const bool validInput = input.Valid &&
            input.NearPlane > 0.f && input.FarPlane > input.NearPlane &&
            Detail::IsFinite(input.View) && Detail::IsFinite(input.Projection) &&
            Detail::IsFinite(input.Position) && Detail::IsFinite(input.Forward) &&
            Detail::IsFinite(input.Up) &&
            glm::length(input.Forward) > 0.000001f &&
            glm::length(input.Up) > 0.000001f;

        if (!validInput)
            return snapshot;

        const float determinant = glm::determinant(snapshot.ViewProjection);
        if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
            return snapshot;

        snapshot.InverseViewProjection = glm::inverse(snapshot.ViewProjection);
        snapshot.Valid = Detail::IsFinite(snapshot.InverseViewProjection);
        if (!snapshot.Valid)
            return snapshot;

        const glm::vec4 row0 = Detail::Row(snapshot.ViewProjection, 0);
        const glm::vec4 row1 = Detail::Row(snapshot.ViewProjection, 1);
        const glm::vec4 row2 = Detail::Row(snapshot.ViewProjection, 2);
        const glm::vec4 row3 = Detail::Row(snapshot.ViewProjection, 3);
        snapshot.FrustumPlanes[static_cast<std::uint32_t>(FrustumPlaneIndex::Left)] = Detail::NormalizePlane(row3 + row0);
        snapshot.FrustumPlanes[static_cast<std::uint32_t>(FrustumPlaneIndex::Right)] = Detail::NormalizePlane(row3 - row0);
        snapshot.FrustumPlanes[static_cast<std::uint32_t>(FrustumPlaneIndex::Bottom)] = Detail::NormalizePlane(row3 + row1);
        snapshot.FrustumPlanes[static_cast<std::uint32_t>(FrustumPlaneIndex::Top)] = Detail::NormalizePlane(row3 - row1);
        snapshot.FrustumPlanes[static_cast<std::uint32_t>(FrustumPlaneIndex::Near)] = Detail::NormalizePlane(row2);
        snapshot.FrustumPlanes[static_cast<std::uint32_t>(FrustumPlaneIndex::Far)] = Detail::NormalizePlane(row3 - row2);

        const std::uint32_t viewportWidth = viewport.Width > 0
            ? static_cast<std::uint32_t>(viewport.Width)
            : 0u;
        const std::uint32_t viewportHeight = viewport.Height > 0
            ? static_cast<std::uint32_t>(viewport.Height)
            : 0u;
        if (pick.Pending && viewportWidth > 0u && viewportHeight > 0u &&
            pick.X < viewportWidth && pick.Y < viewportHeight)
        {
            const float ndcX = ((static_cast<float>(pick.X) + 0.5f) / static_cast<float>(viewportWidth)) * 2.f - 1.f;
            const float ndcY = 1.f - ((static_cast<float>(pick.Y) + 0.5f) / static_cast<float>(viewportHeight)) * 2.f;
            const glm::vec4 nearClip{ndcX, ndcY, 0.f, 1.f};
            const glm::vec4 farClip{ndcX, ndcY, 1.f, 1.f};
            glm::vec4 nearWorld = snapshot.InverseViewProjection * nearClip;
            glm::vec4 farWorld = snapshot.InverseViewProjection * farClip;
            if (std::abs(nearWorld.w) > 0.000001f && std::abs(farWorld.w) > 0.000001f)
            {
                nearWorld /= nearWorld.w;
                farWorld /= farWorld.w;
                const glm::vec3 direction = glm::vec3{farWorld - nearWorld};
                const float directionLength = glm::length(direction);
                if (std::isfinite(directionLength) && directionLength > 0.000001f)
                {
                    snapshot.PickRayOrigin = glm::vec3{nearWorld};
                    snapshot.PickRayDirection = direction / directionLength;
                    snapshot.HasPickRay = true;
                }
            }
        }

        return snapshot;
    }
}

