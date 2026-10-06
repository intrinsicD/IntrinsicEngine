// DeferredSystem: renderer-owned initialization gate for the deferred passes.
// Passes record nothing while the system is not initialized.
export module Extrinsic.Graphics.DeferredSystem;

export namespace Extrinsic::Graphics
{
    class DeferredSystem
    {
    public:
        DeferredSystem() = default;

        DeferredSystem(const DeferredSystem&)            = delete;
        DeferredSystem& operator=(const DeferredSystem&) = delete;

        void Initialize() noexcept { m_Initialized = true; }
        void Shutdown() noexcept { m_Initialized = false; }

        [[nodiscard]] bool IsInitialized() const noexcept { return m_Initialized; }

    private:
        bool m_Initialized{false};
    };
}
