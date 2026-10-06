// ForwardSystem: renderer-owned initialization gate for the forward passes.
// Passes record nothing while the system is not initialized.
export module Extrinsic.Graphics.ForwardSystem;

export namespace Extrinsic::Graphics
{
    class ForwardSystem
    {
    public:
        ForwardSystem() = default;

        ForwardSystem(const ForwardSystem&)            = delete;
        ForwardSystem& operator=(const ForwardSystem&) = delete;

        void Initialize() noexcept { m_Initialized = true; }
        void Shutdown() noexcept { m_Initialized = false; }

        [[nodiscard]] bool IsInitialized() const noexcept { return m_Initialized; }

    private:
        bool m_Initialized{false};
    };
}
