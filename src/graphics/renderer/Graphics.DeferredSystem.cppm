module;

#include <memory>

export module Extrinsic.Graphics.DeferredSystem;

export namespace Extrinsic::Graphics
{
    class DeferredSystem
    {
    public:
        DeferredSystem();
        ~DeferredSystem();

        DeferredSystem(const DeferredSystem&)            = delete;
        DeferredSystem& operator=(const DeferredSystem&) = delete;

        void Initialize();
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
