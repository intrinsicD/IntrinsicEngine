module;

#include <memory>

export module Extrinsic.Graphics.ForwardSystem;

export namespace Extrinsic::Graphics
{
    class ForwardSystem
    {
    public:
        ForwardSystem();
        ~ForwardSystem();

        ForwardSystem(const ForwardSystem&)            = delete;
        ForwardSystem& operator=(const ForwardSystem&) = delete;

        void Initialize();
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

