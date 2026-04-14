//
// Created by alex on 14.04.26.
//
module;

#include <memory>

module Extrinsic.Platform.Window;

namespace Extrinsic::Platform
{
    class NullWindow final : public IWindow
    {
    public:
        explicit NullWindow(const Extrinsic::Core::WindowConfig& config)
            : m_Extent{.Width = config.Width, .Height = config.Height}
        {
        }

        void PollEvents() override {}

        bool ShouldClose() const override
        {
            return m_ShouldClose;
        }

        bool IsMinimized() const override
        {
            return m_Extent.Width == 0 || m_Extent.Height == 0;
        }

        bool WasResized() const override
        {
            return m_WasResized;
        }

        void AcknowledgeResize() override
        {
            m_WasResized = false;
        }

        Extent2D GetExtent() const override
        {
            return m_Extent;
        }

        void* GetNativeHandle() const override
        {
            return nullptr;
        }

    private:
        Extent2D m_Extent{};
        bool m_ShouldClose{false};
        bool m_WasResized{false};
    };

    std::unique_ptr<IWindow> CreateWindow(const Extrinsic::Core::WindowConfig& config)
    {
        return std::make_unique<NullWindow>(config);
    }
}
