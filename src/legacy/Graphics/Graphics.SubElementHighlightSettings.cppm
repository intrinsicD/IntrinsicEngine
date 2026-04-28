module;

#include <cstdint>
#include <glm/glm.hpp>

export module Graphics.SubElementHighlightSettings;

export namespace Graphics
{
    // Configurable appearance settings for sub-element selection highlights
    // (vertices as spheres, edges as overlay lines, faces as tinted triangles).
    // Owned by Runtime::SelectionModule, consumed by EditorUI::DrawSubElementHighlights().
    // Not serialized — editor session preference only.
    struct SubElementHighlightSettings
    {
        // Vertex highlighting
        glm::vec4 VertexColor{1.0f, 0.157f, 0.157f, 1.0f};           // Red  (255, 40, 40)
        glm::vec4 VertexGeodesicColor{0.157f, 1.0f, 0.471f, 1.0f};   // Green (40, 255, 120)
        float     VertexSphereRadius   = 0.005f;                      // Valid: [0.0001, 0.05]
        uint32_t  VertexSphereSegments = 24;                          // Valid: [6, 48]

        // Edge highlighting
        glm::vec4 EdgeColor{1.0f, 0.784f, 0.157f, 1.0f};             // Yellow (255, 200, 40)

        // Face highlighting (outline and fill can differ)
        glm::vec4 FaceOutlineColor{0.157f, 0.471f, 1.0f, 1.0f};      // Blue, fully opaque outline
        glm::vec4 FaceFillColor{0.157f, 0.471f, 1.0f, 0.25f};        // Blue, semi-transparent fill
    };
}
