module;

#include <glm/glm.hpp>

export module Graphics:Passes.SelectionOutlineSettings;

export namespace Graphics::Passes
{
    // Configuration settings for the selection outline effect
    struct SelectionOutlineSettings
    {
        glm::vec4 SelectionColor{1.0f, 0.6f, 0.0f, 1.0f};   // Orange
        glm::vec4 HoverColor{0.3f, 0.7f, 1.0f, 0.8f};       // Light blue, semi-transparent
        float OutlineWidth = 2.0f;                           // Width in texels
    };
}

