module;

#include <memory>

export module Extrinsic.Graphics.ShadowSystem;

export namespace Extrinsic::Graphics
{
    class ShadowSystem
    {
    public:
        ShadowSystem();
        ~ShadowSystem();

        ShadowSystem(const ShadowSystem&)            = delete;
        ShadowSystem& operator=(const ShadowSystem&) = delete;

        void Initialize();
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
