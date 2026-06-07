module;

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

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

    export [[nodiscard]] Graphics::CameraViewInput DefaultCameraControllerSeed() noexcept;

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
        explicit OrbitCameraController(
            const Graphics::CameraViewInput& seed = DefaultCameraControllerSeed()) noexcept;

        void Seed(const Graphics::CameraViewInput& seed) noexcept override;
        void Update(const Platform::Input::Context& input, double deltaSeconds) noexcept override;
        [[nodiscard]] Graphics::CameraViewInput GetView(Core::Extent2D viewport) const noexcept override;
        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override;

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
        float m_NearPlane{0.1f};
        float m_FarPlane{1000.0f};
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
        explicit FlyCameraController(
            const Graphics::CameraViewInput& seed = DefaultCameraControllerSeed()) noexcept;

        void Seed(const Graphics::CameraViewInput& seed) noexcept override;
        void Update(const Platform::Input::Context& input, double deltaSeconds) noexcept override;
        [[nodiscard]] Graphics::CameraViewInput GetView(Core::Extent2D viewport) const noexcept override;
        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override;

    private:
        glm::vec3 m_Position{0.0f, 0.0f, 4.0f};
        float m_Yaw{0.0f};
        float m_Pitch{0.0f};
        float m_NearPlane{0.1f};
        float m_FarPlane{1000.0f};
        float m_MoveSpeed{5.0f};
        float m_MouseSensitivityDegrees{0.1f};
        float m_LastX{0.0f};
        float m_LastY{0.0f};
        bool m_FirstMouse{true};
    };

    export class FreeLookCameraController final : public ICameraController
    {
    public:
        explicit FreeLookCameraController(
            const Graphics::CameraViewInput& seed = DefaultCameraControllerSeed()) noexcept;

        void Seed(const Graphics::CameraViewInput& seed) noexcept override;
        void Update(const Platform::Input::Context& input, double deltaSeconds) noexcept override;
        [[nodiscard]] Graphics::CameraViewInput GetView(Core::Extent2D viewport) const noexcept override;
        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override;

    private:
        glm::vec3 m_Position{0.0f, 0.0f, 4.0f};
        float m_Yaw{0.0f};
        float m_Pitch{0.0f};
        float m_Roll{0.0f};
        float m_NearPlane{0.1f};
        float m_FarPlane{1000.0f};
        float m_MoveSpeed{5.0f};
        float m_MouseSensitivityDegrees{0.1f};
        float m_RollSpeedRadians{3.14159265358979323846f};
        float m_LastX{0.0f};
        float m_LastY{0.0f};
        bool m_FirstMouse{true};
    };

    export class TopDownCameraController final : public ICameraController
    {
    public:
        explicit TopDownCameraController(
            const Graphics::CameraViewInput& seed = DefaultCameraControllerSeed()) noexcept;

        void Seed(const Graphics::CameraViewInput& seed) noexcept override;
        void Update(const Platform::Input::Context& input, double deltaSeconds) noexcept override;
        [[nodiscard]] Graphics::CameraViewInput GetView(Core::Extent2D viewport) const noexcept override;
        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override;

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
        float m_NearPlane{0.1f};
        float m_FarPlane{1000.0f};
        float m_PanSpeed{5.0f};
        float m_ZoomSpeed{1.0f};
    };

    export [[nodiscard]] std::unique_ptr<ICameraController> CreateCameraController(
        Core::Config::CameraControllerKind kind,
        const Graphics::CameraViewInput& seed = DefaultCameraControllerSeed());

    export class CameraControllerRegistry
    {
    public:
        void Register(CameraControllerSlot slot, std::unique_ptr<ICameraController> controller);
        void Replace(CameraControllerSlot slot, std::unique_ptr<ICameraController> controller);
        void MarkCameraTransition(CameraControllerSlot slot) noexcept;
        [[nodiscard]] bool ConsumeCameraTransition(CameraControllerSlot slot) noexcept;
        [[nodiscard]] ICameraController& Resolve(CameraControllerSlot slot);
        [[nodiscard]] ICameraController* ResolveOrNull(CameraControllerSlot slot) noexcept;

    private:
        struct Slot
        {
            CameraControllerSlot SlotId{};
            std::unique_ptr<ICameraController> Controller;
            bool PendingCameraTransition{false};
        };

        std::vector<Slot> m_Slots;
    };
}
