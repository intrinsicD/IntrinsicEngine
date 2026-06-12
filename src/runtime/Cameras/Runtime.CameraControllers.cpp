module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.CameraControllers;

namespace Extrinsic::Runtime
{
    namespace Detail
    {
        constexpr float kDefaultFovYRadians = std::numbers::pi_v<float> / 4.0f;
        constexpr float kDefaultNearPlane = 0.1f;
        constexpr float kDefaultFarPlane = 1000.0f;
        constexpr float kMinFocusRadius = 0.05f;
        constexpr float kFocusPadding = 1.35f;
        constexpr float kMinPitchRadians = -89.0f * std::numbers::pi_v<float> / 180.0f;
        constexpr float kMaxPitchRadians = 89.0f * std::numbers::pi_v<float> / 180.0f;
        constexpr float kTau = std::numbers::pi_v<float> * 2.0f;
        constexpr int kMouseButtonRight = 1;
        constexpr int kMouseButtonMiddle = 2;

        [[nodiscard]] bool IsCameraRotateDragActive(const Platform::Input::Context& input) noexcept
        {
            return input.IsMouseButtonPressed(kMouseButtonRight) ||
                   input.IsMouseButtonPressed(kMouseButtonMiddle);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] float WrapRadians(float value) noexcept
        {
            value = std::fmod(value, kTau);
            if (value < 0.0f)
                value += kTau;
            return value;
        }

        [[nodiscard]] float SafeAspect(const Core::Extent2D viewport) noexcept
        {
            const float width = static_cast<float>(viewport.Width > 0 ? viewport.Width : 1);
            const float height = static_cast<float>(viewport.Height > 0 ? viewport.Height : 1);
            return width / height;
        }

        [[nodiscard]] glm::mat4 MakePerspectiveProjection(const Core::Extent2D viewport,
                                                          const float nearPlane,
                                                          const float farPlane) noexcept
        {
            const float safeNear = std::max(0.0001f, nearPlane);
            const float safeFar = std::max(safeNear + 0.001f, farPlane);
            glm::mat4 projection = glm::perspective(kDefaultFovYRadians, SafeAspect(viewport), safeNear, safeFar);
            projection[1][1] *= -1.0f;
            return projection;
        }

        [[nodiscard]] glm::mat4 MakeOrthographicProjection(const Core::Extent2D viewport,
                                                           const float height,
                                                           const float nearPlane,
                                                           const float farPlane) noexcept
        {
            const float safeNear = std::max(0.0001f, nearPlane);
            const float safeFar = std::max(safeNear + 0.001f, farPlane);
            const float safeHeight = std::max(0.0001f, height);
            const float halfHeight = safeHeight * 0.5f;
            const float halfWidth = halfHeight * SafeAspect(viewport);
            glm::mat4 projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, safeNear, safeFar);
            projection[1][1] *= -1.0f;
            return projection;
        }

        [[nodiscard]] glm::vec3 SafeNormalized(const glm::vec3 value,
                                               const glm::vec3 fallback) noexcept
        {
            const float len = glm::length(value);
            if (!std::isfinite(len) || len <= 0.000001f)
                return fallback;
            return value / len;
        }

        [[nodiscard]] glm::quat SafeNormalized(const glm::quat value,
                                               const glm::quat fallback) noexcept
        {
            const float len = glm::length(value);
            if (!std::isfinite(len) || len <= 0.000001f)
                return fallback;
            return glm::normalize(value);
        }

        [[nodiscard]] float SafeFocusRadius(const CameraFocusTarget target) noexcept
        {
            if (!std::isfinite(target.Radius) || target.Radius <= 0.0f)
                return kMinFocusRadius;
            return std::max(kMinFocusRadius, target.Radius);
        }

        [[nodiscard]] float PerspectiveFocusDistance(const float radius) noexcept
        {
            const float halfFov = kDefaultFovYRadians * 0.5f;
            return std::max(kMinFocusRadius,
                            (radius * kFocusPadding) / std::sin(halfFov));
        }

        [[nodiscard]] float FocusFarPlane(const float distance, const float radius) noexcept
        {
            return std::max(kDefaultFarPlane, distance + radius * 4.0f + 1.0f);
        }

        [[nodiscard]] Graphics::CameraViewInput DefaultSeed() noexcept
        {
            Graphics::CameraViewInput seed{};
            seed.Position = {0.0f, 0.0f, 4.0f};
            seed.Forward = {0.0f, 0.0f, -1.0f};
            seed.Up = {0.0f, 1.0f, 0.0f};
            seed.NearPlane = kDefaultNearPlane;
            seed.FarPlane = kDefaultFarPlane;
            seed.Valid = true;
            return seed;
        }

        [[nodiscard]] glm::vec3 ForwardFromYawPitch(float yaw, float pitch) noexcept
        {
            const float cp = std::cos(pitch);
            return SafeNormalized(glm::vec3{
                std::sin(yaw) * cp,
                std::sin(pitch),
                -std::cos(yaw) * cp,
            }, {0.0f, 0.0f, -1.0f});
        }

        [[nodiscard]] float YawFromForward(const glm::vec3 forward) noexcept
        {
            return WrapRadians(std::atan2(forward.x, -forward.z));
        }

        [[nodiscard]] float PitchFromForward(const glm::vec3 forward) noexcept
        {
            const glm::vec3 f = SafeNormalized(forward, {0.0f, 0.0f, -1.0f});
            return std::clamp(std::asin(std::clamp(f.y, -1.0f, 1.0f)), kMinPitchRadians, kMaxPitchRadians);
        }

        [[nodiscard]] glm::vec3 NonParallelUpForForward(const glm::vec3 forward) noexcept
        {
            constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(forward, kWorldUp)) < 0.95f)
                return kWorldUp;
            return {1.0f, 0.0f, 0.0f};
        }

        [[nodiscard]] glm::quat OrientationFromForwardUp(const glm::vec3 forward,
                                                         const glm::vec3 up) noexcept
        {
            const glm::vec3 f = SafeNormalized(forward, {0.0f, 0.0f, -1.0f});
            glm::vec3 upCandidate = SafeNormalized(up, NonParallelUpForForward(f));
            glm::vec3 right = glm::cross(f, upCandidate);
            if (!IsFinite(right) || glm::length(right) <= 0.000001f)
            {
                upCandidate = NonParallelUpForForward(f);
                right = glm::cross(f, upCandidate);
            }

            right = SafeNormalized(right, {1.0f, 0.0f, 0.0f});
            const glm::vec3 correctedUp = SafeNormalized(glm::cross(right, f), NonParallelUpForForward(f));

            glm::mat3 basis{1.0f};
            basis[0] = right;
            basis[1] = correctedUp;
            basis[2] = -f;
            return SafeNormalized(glm::quat_cast(basis), glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
        }

        [[nodiscard]] glm::vec3 ForwardFromOrientation(const glm::quat orientation) noexcept
        {
            return SafeNormalized(orientation * glm::vec3{0.0f, 0.0f, -1.0f},
                                  {0.0f, 0.0f, -1.0f});
        }

        [[nodiscard]] glm::vec3 RightFromOrientation(const glm::quat orientation) noexcept
        {
            return SafeNormalized(orientation * glm::vec3{1.0f, 0.0f, 0.0f},
                                  {1.0f, 0.0f, 0.0f});
        }

        [[nodiscard]] glm::vec3 UpFromOrientation(const glm::quat orientation) noexcept
        {
            return SafeNormalized(orientation * glm::vec3{0.0f, 1.0f, 0.0f},
                                  {0.0f, 1.0f, 0.0f});
        }

        [[nodiscard]] glm::vec3 RightFromForward(const glm::vec3 forward) noexcept
        {
            return SafeNormalized(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
        }

        [[nodiscard]] glm::vec3 UpFromForwardRight(const glm::vec3 forward,
                                                   const glm::vec3 right) noexcept
        {
            return SafeNormalized(glm::cross(right, forward), {0.0f, 1.0f, 0.0f});
        }

        [[nodiscard]] glm::vec3 RolledRight(const glm::vec3 right,
                                            const glm::vec3 up,
                                            const float roll) noexcept
        {
            return SafeNormalized(right * std::cos(roll) + up * std::sin(roll), {1.0f, 0.0f, 0.0f});
        }

        [[nodiscard]] glm::vec3 RolledUp(const glm::vec3 right,
                                         const glm::vec3 up,
                                         const float roll) noexcept
        {
            return SafeNormalized(-right * std::sin(roll) + up * std::cos(roll), {0.0f, 1.0f, 0.0f});
        }
    }

    Graphics::CameraViewInput DefaultCameraControllerSeed() noexcept
    {
        return Detail::DefaultSeed();
    }

    OrbitCameraController::OrbitCameraController(const Graphics::CameraViewInput& seed) noexcept
    {
        Seed(seed);
    }

    void OrbitCameraController::Seed(const Graphics::CameraViewInput& seed) noexcept
    {
        const Graphics::CameraViewInput safeSeed = seed.Valid ? seed : Detail::DefaultSeed();
        const glm::vec3 forward = Detail::SafeNormalized(safeSeed.Forward, {0.0f, 0.0f, -1.0f});
        const float seedDistance = glm::length(safeSeed.Position);
        m_Radius = std::clamp(seedDistance > m_MinRadius ? seedDistance : m_Radius, m_MinRadius, m_MaxRadius);
        m_Target = safeSeed.Position + forward * m_Radius;
        m_Orientation = Detail::OrientationFromForwardUp(forward, safeSeed.Up);
        m_Yaw = Detail::YawFromForward(Detail::ForwardFromOrientation(m_Orientation));
        m_NearPlane = safeSeed.NearPlane > 0.0f ? safeSeed.NearPlane : Detail::kDefaultNearPlane;
        m_FarPlane = safeSeed.FarPlane > m_NearPlane ? safeSeed.FarPlane : Detail::kDefaultFarPlane;
        m_FirstMouse = true;
    }

    void OrbitCameraController::Focus(const CameraFocusTarget target) noexcept
    {
        if (!Detail::IsFinite(target.Center))
            return;

        const float radius = Detail::SafeFocusRadius(target);
        const float distance = std::clamp(Detail::PerspectiveFocusDistance(radius),
                                          m_MinRadius,
                                          m_MaxRadius);
        m_Target = target.Center;
        m_Radius = distance;
        m_FarPlane = Detail::FocusFarPlane(distance, radius);
        m_FirstMouse = true;
    }

    void OrbitCameraController::Update(const Platform::Input::Context& input,
                                       const double deltaSeconds) noexcept
    {
        const float dt = static_cast<float>(std::max(0.0, deltaSeconds));

        if (Detail::IsCameraRotateDragActive(input))
        {
            const auto pos = input.GetMousePosition();
            if (m_FirstMouse)
            {
                m_LastX = pos.x;
                m_LastY = pos.y;
                m_FirstMouse = false;
            }

            const float xDelta = (pos.x - m_LastX) * m_RotateSensitivityDegrees;
            const float yDelta = (pos.y - m_LastY) * m_RotateSensitivityDegrees;
            const glm::vec3 right = Detail::RightFromOrientation(m_Orientation);
            const glm::vec3 up = Detail::UpFromOrientation(m_Orientation);
            const glm::quat yawRotation = glm::angleAxis(glm::radians(-xDelta), up);
            const glm::quat pitchRotation = glm::angleAxis(glm::radians(yDelta), right);
            m_Orientation = Detail::SafeNormalized(yawRotation * pitchRotation * m_Orientation,
                                                   m_Orientation);
            m_Yaw = Detail::YawFromForward(Detail::ForwardFromOrientation(m_Orientation));
            m_LastX = pos.x;
            m_LastY = pos.y;
        }
        else
        {
            m_FirstMouse = true;
        }

        const auto scroll = input.GetScrollDelta();
        if (scroll.y != 0.0f)
        {
            const float zoom = scroll.y * m_ZoomSpeed * m_Radius * 0.1f;
            m_Radius = std::clamp(m_Radius - zoom, m_MinRadius, m_MaxRadius);
        }

        float velocity = m_PanSpeed * dt;
        if (input.IsKeyPressed(Platform::Input::Key::LeftShift))
            velocity *= 2.5f;

        const glm::vec3 forward = Detail::ForwardFromOrientation(m_Orientation);
        const glm::vec3 up = Detail::UpFromOrientation(m_Orientation);
        glm::vec3 horizontalForward{forward.x, 0.0f, forward.z};
        if (glm::length(horizontalForward) < 0.001f)
            horizontalForward = {up.x, 0.0f, up.z};
        horizontalForward = Detail::SafeNormalized(horizontalForward, {0.0f, 0.0f, -1.0f});

        const glm::vec3 right = Detail::RightFromOrientation(m_Orientation);
        glm::vec3 horizontalRight{right.x, 0.0f, right.z};
        horizontalRight = Detail::SafeNormalized(horizontalRight, {1.0f, 0.0f, 0.0f});

        glm::vec3 pan{0.0f};
        if (input.IsKeyPressed(Platform::Input::Key::W)) pan += horizontalForward * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::S)) pan -= horizontalForward * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::D)) pan += horizontalRight * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::A)) pan -= horizontalRight * velocity;
        m_Target += pan;
    }

    Graphics::CameraViewInput OrbitCameraController::GetView(const Core::Extent2D viewport) const noexcept
    {
        const glm::vec3 forward = Detail::ForwardFromOrientation(m_Orientation);
        const glm::vec3 up = Detail::UpFromOrientation(m_Orientation);
        const glm::vec3 position = m_Target - forward * m_Radius;
        Graphics::CameraViewInput view{};
        view.Position = position;
        view.Forward = forward;
        view.Up = up;
        view.NearPlane = m_NearPlane;
        view.FarPlane = m_FarPlane;
        view.View = glm::lookAt(position, position + forward, view.Up);
        view.Projection = Detail::MakePerspectiveProjection(viewport, view.NearPlane, view.FarPlane);
        view.Valid = true;
        return view;
    }

    Core::Config::CameraControllerKind OrbitCameraController::Kind() const noexcept
    {
        return Core::Config::CameraControllerKind::Orbit;
    }

    FlyCameraController::FlyCameraController(const Graphics::CameraViewInput& seed) noexcept
    {
        Seed(seed);
    }

    void FlyCameraController::Seed(const Graphics::CameraViewInput& seed) noexcept
    {
        const Graphics::CameraViewInput safeSeed = seed.Valid ? seed : Detail::DefaultSeed();
        const glm::vec3 forward = Detail::SafeNormalized(safeSeed.Forward, {0.0f, 0.0f, -1.0f});
        m_Position = safeSeed.Position;
        m_Yaw = Detail::YawFromForward(forward);
        m_Pitch = Detail::PitchFromForward(forward);
        m_NearPlane = safeSeed.NearPlane > 0.0f ? safeSeed.NearPlane : Detail::kDefaultNearPlane;
        m_FarPlane = safeSeed.FarPlane > m_NearPlane ? safeSeed.FarPlane : Detail::kDefaultFarPlane;
        m_FirstMouse = true;
    }

    void FlyCameraController::Focus(const CameraFocusTarget target) noexcept
    {
        if (!Detail::IsFinite(target.Center))
            return;

        const float radius = Detail::SafeFocusRadius(target);
        const float distance = Detail::PerspectiveFocusDistance(radius);
        const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
        m_Position = target.Center - forward * distance;
        m_FarPlane = Detail::FocusFarPlane(distance, radius);
        m_FirstMouse = true;
    }

    void FlyCameraController::Update(const Platform::Input::Context& input,
                                     const double deltaSeconds) noexcept
    {
        if (Detail::IsCameraRotateDragActive(input))
        {
            const auto pos = input.GetMousePosition();
            if (m_FirstMouse)
            {
                m_LastX = pos.x;
                m_LastY = pos.y;
                m_FirstMouse = false;
            }

            const float xOffset = (pos.x - m_LastX) * m_MouseSensitivityDegrees;
            const float yOffset = (pos.y - m_LastY) * m_MouseSensitivityDegrees;
            m_Yaw = Detail::WrapRadians(m_Yaw - glm::radians(xOffset));
            m_Pitch = std::clamp(m_Pitch - glm::radians(yOffset), Detail::kMinPitchRadians, Detail::kMaxPitchRadians);
            m_LastX = pos.x;
            m_LastY = pos.y;
        }
        else
        {
            m_FirstMouse = true;
        }

        float velocity = m_MoveSpeed * static_cast<float>(std::max(0.0, deltaSeconds));
        if (input.IsKeyPressed(Platform::Input::Key::LeftShift))
            velocity *= 2.0f;

        const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
        const glm::vec3 right = Detail::SafeNormalized(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}),
                                                       {1.0f, 0.0f, 0.0f});

        if (input.IsKeyPressed(Platform::Input::Key::W)) m_Position += forward * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::S)) m_Position -= forward * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::D)) m_Position += right * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::A)) m_Position -= right * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::Space)) m_Position += glm::vec3{0.0f, 1.0f, 0.0f} * velocity;
    }

    Graphics::CameraViewInput FlyCameraController::GetView(const Core::Extent2D viewport) const noexcept
    {
        const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
        Graphics::CameraViewInput view{};
        view.Position = m_Position;
        view.Forward = forward;
        view.Up = {0.0f, 1.0f, 0.0f};
        view.NearPlane = m_NearPlane;
        view.FarPlane = m_FarPlane;
        view.View = glm::lookAt(m_Position, m_Position + forward, view.Up);
        view.Projection = Detail::MakePerspectiveProjection(viewport, view.NearPlane, view.FarPlane);
        view.Valid = true;
        return view;
    }

    Core::Config::CameraControllerKind FlyCameraController::Kind() const noexcept
    {
        return Core::Config::CameraControllerKind::Fly;
    }

    FreeLookCameraController::FreeLookCameraController(const Graphics::CameraViewInput& seed) noexcept
    {
        Seed(seed);
    }

    void FreeLookCameraController::Seed(const Graphics::CameraViewInput& seed) noexcept
    {
        const Graphics::CameraViewInput safeSeed = seed.Valid ? seed : Detail::DefaultSeed();
        const glm::vec3 forward = Detail::SafeNormalized(safeSeed.Forward, {0.0f, 0.0f, -1.0f});
        m_Position = safeSeed.Position;
        m_Yaw = Detail::YawFromForward(forward);
        m_Pitch = Detail::PitchFromForward(forward);
        m_Roll = 0.0f;
        m_NearPlane = safeSeed.NearPlane > 0.0f ? safeSeed.NearPlane : Detail::kDefaultNearPlane;
        m_FarPlane = safeSeed.FarPlane > m_NearPlane ? safeSeed.FarPlane : Detail::kDefaultFarPlane;
        m_FirstMouse = true;
    }

    void FreeLookCameraController::Focus(const CameraFocusTarget target) noexcept
    {
        if (!Detail::IsFinite(target.Center))
            return;

        const float radius = Detail::SafeFocusRadius(target);
        const float distance = Detail::PerspectiveFocusDistance(radius);
        const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
        m_Position = target.Center - forward * distance;
        m_FarPlane = Detail::FocusFarPlane(distance, radius);
        m_FirstMouse = true;
    }

    void FreeLookCameraController::Update(const Platform::Input::Context& input,
                                          const double deltaSeconds) noexcept
    {
        if (Detail::IsCameraRotateDragActive(input))
        {
            const auto pos = input.GetMousePosition();
            if (m_FirstMouse)
            {
                m_LastX = pos.x;
                m_LastY = pos.y;
                m_FirstMouse = false;
            }

            const float xOffset = (pos.x - m_LastX) * m_MouseSensitivityDegrees;
            const float yOffset = (pos.y - m_LastY) * m_MouseSensitivityDegrees;
            m_Yaw = Detail::WrapRadians(m_Yaw - glm::radians(xOffset));
            m_Pitch = std::clamp(m_Pitch - glm::radians(yOffset), Detail::kMinPitchRadians, Detail::kMaxPitchRadians);
            m_LastX = pos.x;
            m_LastY = pos.y;
        }
        else
        {
            m_FirstMouse = true;
        }

        const float dt = static_cast<float>(std::max(0.0, deltaSeconds));
        float velocity = m_MoveSpeed * dt;
        if (input.IsKeyPressed(Platform::Input::Key::LeftShift))
            velocity *= 2.0f;

        if (input.IsKeyPressed(Platform::Input::Key::Q)) m_Roll = Detail::WrapRadians(m_Roll + m_RollSpeedRadians * dt);
        if (input.IsKeyPressed(Platform::Input::Key::E)) m_Roll = Detail::WrapRadians(m_Roll - m_RollSpeedRadians * dt);

        const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
        const glm::vec3 baseRight = Detail::RightFromForward(forward);
        const glm::vec3 baseUp = Detail::UpFromForwardRight(forward, baseRight);
        const glm::vec3 right = Detail::RolledRight(baseRight, baseUp, m_Roll);
        const glm::vec3 up = Detail::RolledUp(baseRight, baseUp, m_Roll);

        if (input.IsKeyPressed(Platform::Input::Key::W)) m_Position += forward * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::S)) m_Position -= forward * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::D)) m_Position += right * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::A)) m_Position -= right * velocity;
        if (input.IsKeyPressed(Platform::Input::Key::Space)) m_Position += up * velocity;
    }

    Graphics::CameraViewInput FreeLookCameraController::GetView(const Core::Extent2D viewport) const noexcept
    {
        const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
        const glm::vec3 baseRight = Detail::RightFromForward(forward);
        const glm::vec3 baseUp = Detail::UpFromForwardRight(forward, baseRight);

        Graphics::CameraViewInput view{};
        view.Position = m_Position;
        view.Forward = forward;
        view.Up = Detail::RolledUp(baseRight, baseUp, m_Roll);
        view.NearPlane = m_NearPlane;
        view.FarPlane = m_FarPlane;
        view.View = glm::lookAt(m_Position, m_Position + forward, view.Up);
        view.Projection = Detail::MakePerspectiveProjection(viewport, view.NearPlane, view.FarPlane);
        view.Valid = true;
        return view;
    }

    Core::Config::CameraControllerKind FreeLookCameraController::Kind() const noexcept
    {
        return Core::Config::CameraControllerKind::FreeLook;
    }

    TopDownCameraController::TopDownCameraController(const Graphics::CameraViewInput& seed) noexcept
    {
        Seed(seed);
    }

    void TopDownCameraController::Seed(const Graphics::CameraViewInput& seed) noexcept
    {
        const Graphics::CameraViewInput safeSeed = seed.Valid ? seed : Detail::DefaultSeed();
        const glm::vec3 forward = Detail::SafeNormalized(safeSeed.Forward, {0.0f, 0.0f, -1.0f});
        const float seedDistance = glm::length(safeSeed.Position);
        const float focusDistance =
            std::isfinite(seedDistance) && seedDistance > m_MinAltitude
                ? seedDistance
                : m_Altitude;
        m_Altitude = std::clamp(focusDistance, m_MinAltitude, m_MaxAltitude);
        const glm::vec3 focus = safeSeed.Position + forward * m_Altitude;
        m_Target = {focus.x, 0.0f, focus.z};
        m_OrthographicHeight = std::clamp(m_Altitude * 2.0f, m_MinOrthographicHeight, m_MaxOrthographicHeight);
        m_NearPlane = safeSeed.NearPlane > 0.0f ? safeSeed.NearPlane : Detail::kDefaultNearPlane;
        m_FarPlane = safeSeed.FarPlane > m_NearPlane ? safeSeed.FarPlane : Detail::kDefaultFarPlane;
    }

    void TopDownCameraController::Focus(const CameraFocusTarget target) noexcept
    {
        if (!Detail::IsFinite(target.Center))
            return;

        const float radius = Detail::SafeFocusRadius(target);
        const float height = std::clamp(radius * 2.0f * Detail::kFocusPadding,
                                        m_MinOrthographicHeight,
                                        m_MaxOrthographicHeight);
        m_Target = target.Center;
        m_OrthographicHeight = height;
        m_Altitude = std::clamp(radius * 2.0f * Detail::kFocusPadding,
                                m_MinAltitude,
                                m_MaxAltitude);
        m_FarPlane = Detail::FocusFarPlane(m_Altitude, radius);
    }

    void TopDownCameraController::Update(const Platform::Input::Context& input,
                                         const double deltaSeconds) noexcept
    {
        float velocity = m_PanSpeed * static_cast<float>(std::max(0.0, deltaSeconds));
        if (input.IsKeyPressed(Platform::Input::Key::LeftShift))
            velocity *= 2.5f;

        if (input.IsKeyPressed(Platform::Input::Key::W)) m_Target.z -= velocity;
        if (input.IsKeyPressed(Platform::Input::Key::S)) m_Target.z += velocity;
        if (input.IsKeyPressed(Platform::Input::Key::D)) m_Target.x += velocity;
        if (input.IsKeyPressed(Platform::Input::Key::A)) m_Target.x -= velocity;

        const auto scroll = input.GetScrollDelta();
        if (scroll.y != 0.0f)
        {
            const float zoom = scroll.y * m_ZoomSpeed * m_OrthographicHeight * 0.1f;
            m_OrthographicHeight = std::clamp(m_OrthographicHeight - zoom,
                                              m_MinOrthographicHeight,
                                              m_MaxOrthographicHeight);
        }
    }

    Graphics::CameraViewInput TopDownCameraController::GetView(const Core::Extent2D viewport) const noexcept
    {
        const glm::vec3 position = m_Target + glm::vec3{0.0f, m_Altitude, 0.0f};
        Graphics::CameraViewInput view{};
        view.Position = position;
        view.Forward = {0.0f, -1.0f, 0.0f};
        view.Up = {0.0f, 0.0f, -1.0f};
        view.NearPlane = m_NearPlane;
        view.FarPlane = m_FarPlane;
        view.View = glm::lookAt(position, m_Target, view.Up);
        view.Projection = Detail::MakeOrthographicProjection(viewport,
                                                             m_OrthographicHeight,
                                                             view.NearPlane,
                                                             view.FarPlane);
        view.Valid = true;
        return view;
    }

    Core::Config::CameraControllerKind TopDownCameraController::Kind() const noexcept
    {
        return Core::Config::CameraControllerKind::TopDown;
    }

    std::unique_ptr<ICameraController> CreateCameraController(
        const Core::Config::CameraControllerKind kind,
        const Graphics::CameraViewInput& seed)
    {
        switch (kind)
        {
        case Core::Config::CameraControllerKind::FreeLook:
            return std::make_unique<FreeLookCameraController>(seed);
        case Core::Config::CameraControllerKind::TopDown:
            return std::make_unique<TopDownCameraController>(seed);
        case Core::Config::CameraControllerKind::Fly:
            return std::make_unique<FlyCameraController>(seed);
        case Core::Config::CameraControllerKind::Orbit:
            return std::make_unique<OrbitCameraController>(seed);
        }
        return std::make_unique<OrbitCameraController>(seed);
    }

    void CameraControllerRegistry::Register(CameraControllerSlot slot,
                                            std::unique_ptr<ICameraController> controller)
    {
        if (!controller)
            std::terminate();
        if (ResolveOrNull(slot) != nullptr)
            std::terminate();
        m_Slots.push_back(Slot{.SlotId = slot,
                               .Controller = std::move(controller),
                               .PendingCameraTransition = true});
    }

    void CameraControllerRegistry::Replace(CameraControllerSlot slot,
                                           std::unique_ptr<ICameraController> controller)
    {
        if (!controller)
            std::terminate();
        for (Slot& entry : m_Slots)
        {
            if (entry.SlotId == slot)
            {
                entry.Controller = std::move(controller);
                entry.PendingCameraTransition = true;
                return;
            }
        }
        m_Slots.push_back(Slot{.SlotId = slot,
                               .Controller = std::move(controller),
                               .PendingCameraTransition = true});
    }

    void CameraControllerRegistry::MarkCameraTransition(CameraControllerSlot slot) noexcept
    {
        for (Slot& entry : m_Slots)
        {
            if (entry.SlotId == slot)
            {
                entry.PendingCameraTransition = true;
                return;
            }
        }
    }

    bool CameraControllerRegistry::ConsumeCameraTransition(CameraControllerSlot slot) noexcept
    {
        for (Slot& entry : m_Slots)
        {
            if (entry.SlotId == slot)
            {
                const bool pending = entry.PendingCameraTransition;
                entry.PendingCameraTransition = false;
                return pending;
            }
        }
        return false;
    }

    ICameraController& CameraControllerRegistry::Resolve(CameraControllerSlot slot)
    {
        if (ICameraController* controller = ResolveOrNull(slot))
            return *controller;
        std::terminate();
    }

    ICameraController* CameraControllerRegistry::ResolveOrNull(CameraControllerSlot slot) noexcept
    {
        for (Slot& entry : m_Slots)
        {
            if (entry.SlotId == slot)
                return entry.Controller.get();
        }
        return nullptr;
    }
}
