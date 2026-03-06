module;

#include <glm/glm.hpp>

export module Graphics:Passes.SelectionOutlineSettings;

export namespace Graphics::Passes
{
    // Outline visual style mode
    enum class OutlineMode : uint32_t
    {
        Solid = 0,    // Constant-color edge outline
        Pulse = 1,    // Animated pulsing outline (oscillating alpha)
        Glow  = 2     // Soft glow that fades with distance from edge
    };

    // Configuration settings for the selection outline effect
    struct SelectionOutlineSettings
    {
        glm::vec4 SelectionColor{1.0f, 0.6f, 0.0f, 1.0f};   // Orange
        glm::vec4 HoverColor{0.3f, 0.7f, 1.0f, 0.8f};       // Light blue, semi-transparent
        float OutlineWidth = 2.0f;                           // Width in texels (1-10)

        // Outline mode
        OutlineMode Mode = OutlineMode::Solid;

        // Fill overlay: semi-transparent tint inside selected/hovered entities
        float SelectionFillAlpha = 0.0f;                     // 0 = no fill, 0.1-0.3 typical
        float HoverFillAlpha    = 0.08f;                     // Subtle hover tint by default

        // Pulse mode settings
        float PulseSpeed  = 3.0f;                            // Oscillation speed (Hz-like)
        float PulseMin    = 0.4f;                            // Minimum alpha during pulse
        float PulseMax    = 1.0f;                            // Maximum alpha during pulse

        // Glow mode settings
        float GlowFalloff = 2.0f;                            // Exponential falloff rate
    };
}
