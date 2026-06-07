module;

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

module Extrinsic.Graphics.CameraSnapshots;

namespace Extrinsic::Graphics
{
    namespace Detail
    {
        [[nodiscard]] constexpr float Halton(std::uint32_t index,
                                             const std::uint32_t base) noexcept
        {
            float result = 0.f;
            float invBase = 1.f / static_cast<float>(base);
            while (index > 0u)
            {
                result += static_cast<float>(index % base) * invBase;
                index /= base;
                invBase /= static_cast<float>(base);
            }
            return result;
        }

        [[nodiscard]] constexpr glm::vec4 Row(const glm::mat4& m, const int row) noexcept
        {
            return {m[0][row], m[1][row], m[2][row], m[3][row]};
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] bool IsFinite(const glm::mat4& value) noexcept
        {
            for (int col = 0; col < 4; ++col)
            {
                if (!IsFinite(value[col]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] FrustumPlaneSnapshot NormalizePlane(const glm::vec4 equation) noexcept
        {
            const glm::vec3 normal{equation.x, equation.y, equation.z};
            const float len = glm::length(normal);
            if (!std::isfinite(len) || len <= 0.000001f)
                return {};
            return FrustumPlaneSnapshot{.Equation = equation / len, .Valid = true};
        }

        [[nodiscard]] CameraViewSnapshot BuildCameraViewSnapshotWithProjection(
            const CameraViewInput& input,
            const Core::Extent2D viewport,
            const PickPixelRequest pick,
            const glm::mat4& projection) noexcept
        {
            CameraViewSnapshot snapshot{};

            snapshot.View = input.View;
            snapshot.Projection = projection;
            snapshot.ViewProjection = projection * input.View;
            snapshot.Position = input.Position;
            snapshot.Forward = input.Forward;
            snapshot.Up = input.Up;
            snapshot.NearPlane = input.NearPlane;
            snapshot.FarPlane = input.FarPlane;
            snapshot.ExplicitCameraTransition = input.ExplicitCameraTransition;

            const bool validInput = input.Valid &&
                input.NearPlane > 0.f && input.FarPlane > input.NearPlane &&
                Detail::IsFinite(input.View) && Detail::IsFinite(projection) &&
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

    TemporalJitterSample ComputeTemporalJitterSample(
        const std::uint64_t renderedFrameIndex,
        const Core::Extent2D viewport,
        const bool noJitterNoHistory) noexcept
    {
        const std::uint32_t sequenceIndex =
            static_cast<std::uint32_t>(renderedFrameIndex % kTemporalJitterSequenceLength);
        TemporalJitterSample sample{.SequenceIndex = sequenceIndex};
        if (noJitterNoHistory || viewport.Width == 0u || viewport.Height == 0u)
        {
            return sample;
        }

        const std::uint32_t haltonIndex = sequenceIndex + 1u;
        sample.PixelOffset = glm::vec2{
            Detail::Halton(haltonIndex, 2u) - 0.5f,
            Detail::Halton(haltonIndex, 3u) - 0.5f,
        };
        sample.NdcOffset = glm::vec2{
            sample.PixelOffset.x * (2.f / static_cast<float>(viewport.Width)),
            sample.PixelOffset.y * (2.f / static_cast<float>(viewport.Height)),
        };
        sample.Enabled = true;
        return sample;
    }

    glm::mat4 ApplyTemporalJitterProjectionOverride(glm::mat4 projection,
                                                    const glm::vec2 ndcOffset) noexcept
    {
        projection[2][0] += ndcOffset.x;
        projection[2][1] += ndcOffset.y;
        return projection;
    }

    CameraViewSnapshot BuildCameraViewSnapshot(const CameraViewInput& input,
                                               const Core::Extent2D viewport,
                                               const PickPixelRequest pick) noexcept
    {
        return Detail::BuildCameraViewSnapshotWithProjection(input, viewport, pick, input.Projection);
    }

    TemporalCameraViewSnapshot BuildTemporalCameraViewSnapshot(
        const CameraViewInput& input,
        const Core::Extent2D viewport,
        const PickPixelRequest pick,
        const std::uint64_t renderedFrameIndex,
        const bool enableTemporalJitter,
        const bool noJitterNoHistory) noexcept
    {
        const TemporalJitterSample jitter = enableTemporalJitter
            ? ComputeTemporalJitterSample(renderedFrameIndex, viewport, noJitterNoHistory)
            : TemporalJitterSample{
                .SequenceIndex = static_cast<std::uint32_t>(
                    renderedFrameIndex % kTemporalJitterSequenceLength),
            };
        const glm::mat4 projection = jitter.Enabled
            ? ApplyTemporalJitterProjectionOverride(input.Projection, jitter.NdcOffset)
            : input.Projection;

        return TemporalCameraViewSnapshot{
            .Camera = Detail::BuildCameraViewSnapshotWithProjection(input, viewport, pick, projection),
            .JitterOffset = jitter.NdcOffset,
            .JitterSequenceIndex = jitter.SequenceIndex,
            .HasTemporalJitter = jitter.Enabled,
        };
    }
}
