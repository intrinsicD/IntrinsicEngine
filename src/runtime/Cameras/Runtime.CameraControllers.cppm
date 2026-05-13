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

export module Extrinsic.Runtime.CameraControllers;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Platform.Input;

namespace Extrinsic::Runtime
{
    export enum class CameraControllerSlot : std::uint32_t
    {
        Main = 0,
        Preview = 1,
        TopDown = 2,
        EditorSecondary = 3,
    };

    namespace Detail
    {
        constexpr float kDefaultFovYRadians = std::numbers::pi_v<float> / 4.0f;
        constexpr float kDefaultNearPlane = 0.1f;
        constexpr float kDefaultFarPlane = 1000.0f;
        constexpr float kMinPitchRadians = -89.0f * std::numbers::pi_v<float> / 180.0f;
        constexpr float kMaxPitchRadians = 89.0f * std::numbers::pi_v<float> / 180.0f;
        constexpr float kTau = std::numbers::pi_v<float> * 2.0f;
        constexpr int kMouseButtonRight = 1;

        [[nodiscard]] inline float WrapRadians(float value) noexcept
        {
            value = std::fmod(value, kTau);
            if (value < 0.0f)
                value += kTau;
            return value;
        }

        [[nodiscard]] inline float SafeAspect(const Core::Extent2D viewport) noexcept
        {
            const float width = static_cast<float>(viewport.Width > 0 ? viewport.Width : 1);
            const float height = static_cast<float>(viewport.Height > 0 ? viewport.Height : 1);
            return width / height;
        }

        [[nodiscard]] inline glm::mat4 MakePerspectiveProjection(const Core::Extent2D viewport,
                                                                 const float nearPlane,
                                                                 const float farPlane) noexcept
        {
            const float safeNear = std::max(0.0001f, nearPlane);
            const float safeFar = std::max(safeNear + 0.001f, farPlane);
            glm::mat4 projection = glm::perspective(kDefaultFovYRadians, SafeAspect(viewport), safeNear, safeFar);
            projection[1][1] *= -1.0f;
            return projection;
        }

        [[nodiscard]] inline glm::mat4 MakeOrthographicProjection(const Core::Extent2D viewport,
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

        [[nodiscard]] inline glm::vec3 SafeNormalized(const glm::vec3 value,
                                                      const glm::vec3 fallback) noexcept
        {
            const float len = glm::length(value);
            if (!std::isfinite(len) || len <= 0.000001f)
                return fallback;
            return value / len;
        }

        [[nodiscard]] inline Graphics::CameraViewInput DefaultSeed() noexcept
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

        [[nodiscard]] inline glm::vec3 ForwardFromYawPitch(float yaw, float pitch) noexcept
        {
            const float cp = std::cos(pitch);
            return SafeNormalized(glm::vec3{
                std::sin(yaw) * cp,
                std::sin(pitch),
                -std::cos(yaw) * cp,
            }, {0.0f, 0.0f, -1.0f});
        }

        [[nodiscard]] inline float YawFromForward(const glm::vec3 forward) noexcept
        {
            return WrapRadians(std::atan2(forward.x, -forward.z));
        }

        [[nodiscard]] inline float PitchFromForward(const glm::vec3 forward) noexcept
        {
            const glm::vec3 f = SafeNormalized(forward, {0.0f, 0.0f, -1.0f});
            return std::clamp(std::asin(std::clamp(f.y, -1.0f, 1.0f)), kMinPitchRadians, kMaxPitchRadians);
        }

        [[nodiscard]] inline glm::vec3 RightFromForward(const glm::vec3 forward) noexcept
        {
            return SafeNormalized(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
        }

        [[nodiscard]] inline glm::vec3 UpFromForwardRight(const glm::vec3 forward,
                                                          const glm::vec3 right) noexcept
        {
            return SafeNormalized(glm::cross(right, forward), {0.0f, 1.0f, 0.0f});
        }

        [[nodiscard]] inline glm::vec3 RolledRight(const glm::vec3 right,
                                                   const glm::vec3 up,
                                                   const float roll) noexcept
        {
            return SafeNormalized(right * std::cos(roll) + up * std::sin(roll), {1.0f, 0.0f, 0.0f});
        }

        [[nodiscard]] inline glm::vec3 RolledUp(const glm::vec3 right,
                                                const glm::vec3 up,
                                                const float roll) noexcept
        {
            return SafeNormalized(-right * std::sin(roll) + up * std::cos(roll), {0.0f, 1.0f, 0.0f});
        }
    }

    export class ICameraController
    {
    public:
        virtual ~ICameraController() = default;

        virtual void Seed(const Graphics::CameraViewInput& seed) noexcept = 0;
        virtual void Update(const Platform::Input::Context& input, double deltaSeconds) noexcept = 0;
        [[nodiscard]] virtual Graphics::CameraViewInput GetView(Core::Extent2D viewport) const noexcept = 0;
        [[nodiscard]] virtual Core::Config::CameraControllerKind Kind() const noexcept = 0;
    };

    export class OrbitCameraController final : public ICameraController
    {
    public:
        explicit OrbitCameraController(const Graphics::CameraViewInput& seed = Detail::DefaultSeed()) noexcept
        {
            Seed(seed);
        }

        void Seed(const Graphics::CameraViewInput& seed) noexcept override
        {
            const Graphics::CameraViewInput safeSeed = seed.Valid ? seed : Detail::DefaultSeed();
            const glm::vec3 forward = Detail::SafeNormalized(safeSeed.Forward, {0.0f, 0.0f, -1.0f});
            const float seedDistance = glm::length(safeSeed.Position);
            m_Radius = std::clamp(seedDistance > m_MinRadius ? seedDistance : m_Radius, m_MinRadius, m_MaxRadius);
            m_Target = safeSeed.Position + forward * m_Radius;
            m_Yaw = Detail::YawFromForward(forward);
            m_Pitch = Detail::PitchFromForward(forward);
            m_NearPlane = safeSeed.NearPlane > 0.0f ? safeSeed.NearPlane : Detail::kDefaultNearPlane;
            m_FarPlane = safeSeed.FarPlane > m_NearPlane ? safeSeed.FarPlane : Detail::kDefaultFarPlane;
            m_FirstMouse = true;
        }

        void Update(const Platform::Input::Context& input, const double deltaSeconds) noexcept override
        {
            const float dt = static_cast<float>(std::max(0.0, deltaSeconds));

            if (input.IsMouseButtonPressed(Detail::kMouseButtonRight))
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
                m_Yaw = Detail::WrapRadians(m_Yaw - glm::radians(xDelta));
                m_Pitch = std::clamp(m_Pitch - glm::radians(yDelta), Detail::kMinPitchRadians, Detail::kMaxPitchRadians);
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

            const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
            glm::vec3 horizontalForward{forward.x, 0.0f, forward.z};
            horizontalForward = Detail::SafeNormalized(horizontalForward, {0.0f, 0.0f, -1.0f});
            glm::vec3 horizontalRight = Detail::SafeNormalized(glm::cross(horizontalForward, glm::vec3{0.0f, 1.0f, 0.0f}),
                                                               {1.0f, 0.0f, 0.0f});

            glm::vec3 pan{0.0f};
            if (input.IsKeyPressed(Platform::Input::Key::W)) pan += horizontalForward * velocity;
            if (input.IsKeyPressed(Platform::Input::Key::S)) pan -= horizontalForward * velocity;
            if (input.IsKeyPressed(Platform::Input::Key::D)) pan += horizontalRight * velocity;
            if (input.IsKeyPressed(Platform::Input::Key::A)) pan -= horizontalRight * velocity;
            m_Target += pan;
        }

        [[nodiscard]] Graphics::CameraViewInput GetView(const Core::Extent2D viewport) const noexcept override
        {
            const glm::vec3 forward = Detail::ForwardFromYawPitch(m_Yaw, m_Pitch);
            const glm::vec3 position = m_Target - forward * m_Radius;
            Graphics::CameraViewInput view{};
            view.Position = position;
            view.Forward = forward;
            view.Up = {0.0f, 1.0f, 0.0f};
            view.NearPlane = m_NearPlane;
            view.FarPlane = m_FarPlane;
            view.View = glm::lookAt(position, position + forward, view.Up);
            view.Projection = Detail::MakePerspectiveProjection(viewport, view.NearPlane, view.FarPlane);
            view.Valid = true;
            return view;
        }

        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override
        {
            return Core::Config::CameraControllerKind::Orbit;
        }

        [[nodiscard]] float Radius() const noexcept { return m_Radius; }
        [[nodiscard]] float MinRadius() const noexcept { return m_MinRadius; }
        [[nodiscard]] float YawRadians() const noexcept { return m_Yaw; }

    private:
        glm::vec3 m_Target{0.0f};
        float m_Radius{5.0f};
        float m_MinRadius{0.1f};
        float m_MaxRadius{500.0f};
        float m_Yaw{0.0f};
        float m_Pitch{0.0f};
        float m_NearPlane{Detail::kDefaultNearPlane};
        float m_FarPlane{Detail::kDefaultFarPlane};
        float m_RotateSensitivityDegrees{0.2f};
        float m_ZoomSpeed{1.0f};
        float m_PanSpeed{3.0f};
        float m_LastX{0.0f};
        float m_LastY{0.0f};
        bool m_FirstMouse{true};
    };

    export class FlyCameraController final : public ICameraController
    {
    public:
        explicit FlyCameraController(const Graphics::CameraViewInput& seed = Detail::DefaultSeed()) noexcept
        {
            Seed(seed);
        }

        void Seed(const Graphics::CameraViewInput& seed) noexcept override
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

        void Update(const Platform::Input::Context& input, const double deltaSeconds) noexcept override
        {
            if (input.IsMouseButtonPressed(Detail::kMouseButtonRight))
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

        [[nodiscard]] Graphics::CameraViewInput GetView(const Core::Extent2D viewport) const noexcept override
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

        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override
        {
            return Core::Config::CameraControllerKind::Fly;
        }

    private:
        glm::vec3 m_Position{0.0f, 0.0f, 4.0f};
        float m_Yaw{0.0f};
        float m_Pitch{0.0f};
        float m_NearPlane{Detail::kDefaultNearPlane};
        float m_FarPlane{Detail::kDefaultFarPlane};
        float m_MoveSpeed{5.0f};
        float m_MouseSensitivityDegrees{0.1f};
        float m_LastX{0.0f};
        float m_LastY{0.0f};
        bool m_FirstMouse{true};
    };

    export class FreeLookCameraController final : public ICameraController
    {
    public:
        explicit FreeLookCameraController(const Graphics::CameraViewInput& seed = Detail::DefaultSeed()) noexcept
        {
            Seed(seed);
        }

        void Seed(const Graphics::CameraViewInput& seed) noexcept override
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

        void Update(const Platform::Input::Context& input, const double deltaSeconds) noexcept override
        {
            if (input.IsMouseButtonPressed(Detail::kMouseButtonRight))
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

        [[nodiscard]] Graphics::CameraViewInput GetView(const Core::Extent2D viewport) const noexcept override
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

        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override
        {
            return Core::Config::CameraControllerKind::FreeLook;
        }

    private:
        glm::vec3 m_Position{0.0f, 0.0f, 4.0f};
        float m_Yaw{0.0f};
        float m_Pitch{0.0f};
        float m_Roll{0.0f};
        float m_NearPlane{Detail::kDefaultNearPlane};
        float m_FarPlane{Detail::kDefaultFarPlane};
        float m_MoveSpeed{5.0f};
        float m_MouseSensitivityDegrees{0.1f};
        float m_RollSpeedRadians{std::numbers::pi_v<float>};
        float m_LastX{0.0f};
        float m_LastY{0.0f};
        bool m_FirstMouse{true};
    };

    export class TopDownCameraController final : public ICameraController
    {
    public:
        explicit TopDownCameraController(const Graphics::CameraViewInput& seed = Detail::DefaultSeed()) noexcept
        {
            Seed(seed);
        }

        void Seed(const Graphics::CameraViewInput& seed) noexcept override
        {
            const Graphics::CameraViewInput safeSeed = seed.Valid ? seed : Detail::DefaultSeed();
            m_Target = {safeSeed.Position.x, 0.0f, safeSeed.Position.z};
            m_Altitude = std::clamp(std::max(std::abs(safeSeed.Position.y), glm::length(safeSeed.Position)),
                                    m_MinAltitude,
                                    m_MaxAltitude);
            m_OrthographicHeight = std::clamp(m_Altitude * 2.0f, m_MinOrthographicHeight, m_MaxOrthographicHeight);
            m_NearPlane = safeSeed.NearPlane > 0.0f ? safeSeed.NearPlane : Detail::kDefaultNearPlane;
            m_FarPlane = safeSeed.FarPlane > m_NearPlane ? safeSeed.FarPlane : Detail::kDefaultFarPlane;
        }

        void Update(const Platform::Input::Context& input, const double deltaSeconds) noexcept override
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

        [[nodiscard]] Graphics::CameraViewInput GetView(const Core::Extent2D viewport) const noexcept override
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

        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override
        {
            return Core::Config::CameraControllerKind::TopDown;
        }

        [[nodiscard]] float OrthographicHeight() const noexcept { return m_OrthographicHeight; }
        [[nodiscard]] float MinOrthographicHeight() const noexcept { return m_MinOrthographicHeight; }

    private:
        glm::vec3 m_Target{0.0f};
        float m_Altitude{10.0f};
        float m_MinAltitude{0.1f};
        float m_MaxAltitude{1000.0f};
        float m_OrthographicHeight{10.0f};
        float m_MinOrthographicHeight{0.1f};
        float m_MaxOrthographicHeight{1000.0f};
        float m_NearPlane{Detail::kDefaultNearPlane};
        float m_FarPlane{Detail::kDefaultFarPlane};
        float m_PanSpeed{5.0f};
        float m_ZoomSpeed{1.0f};
    };

    export [[nodiscard]] inline std::unique_ptr<ICameraController> CreateCameraController(
        const Core::Config::CameraControllerKind kind,
        const Graphics::CameraViewInput& seed = Detail::DefaultSeed())
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

    export class CameraControllerRegistry
    {
    public:
        void Register(CameraControllerSlot slot, std::unique_ptr<ICameraController> controller)
        {
            if (!controller)
                std::terminate();
            if (ResolveOrNull(slot) != nullptr)
                std::terminate();
            m_Slots.push_back(Slot{slot, std::move(controller)});
        }

        void Replace(CameraControllerSlot slot, std::unique_ptr<ICameraController> controller)
        {
            if (!controller)
                std::terminate();
            for (Slot& entry : m_Slots)
            {
                if (entry.SlotId == slot)
                {
                    entry.Controller = std::move(controller);
                    return;
                }
            }
            m_Slots.push_back(Slot{slot, std::move(controller)});
        }

        [[nodiscard]] ICameraController& Resolve(CameraControllerSlot slot)
        {
            if (ICameraController* controller = ResolveOrNull(slot))
                return *controller;
            std::terminate();
        }

        [[nodiscard]] ICameraController* ResolveOrNull(CameraControllerSlot slot) noexcept
        {
            for (Slot& entry : m_Slots)
            {
                if (entry.SlotId == slot)
                    return entry.Controller.get();
            }
            return nullptr;
        }

    private:
        struct Slot
        {
            CameraControllerSlot SlotId{};
            std::unique_ptr<ICameraController> Controller;
        };

        std::vector<Slot> m_Slots;
    };
}



