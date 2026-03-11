// Runtime.EditorUI.Widgets — Reusable ImGui widgets for PropertySet-driven
// color source selection and vector field overlay management. Plus editor
// utility functions (matrix/vector comparison, depth ramp, AABB transform).

module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <imgui.h>

module Runtime.EditorUI;

import Graphics;
import Geometry;

namespace Runtime::EditorUI
{

// =========================================================================
// ColorSourceWidget
// =========================================================================
bool ColorSourceWidget(const char* label, Graphics::ColorSource& src,
                       const Geometry::PropertySet* ps, const char* suffix)
{
    bool changed = false;
    char idBuf[128];

    ImGui::SeparatorText(label);

    // Property selector combo.
    if (ps)
    {
        auto props = Graphics::EnumerateColorableProperties(*ps);

        snprintf(idBuf, sizeof(idBuf), "Property##%s", suffix);
        const char* currentName = src.PropertyName.empty() ? "(none)" : src.PropertyName.c_str();
        if (ImGui::BeginCombo(idBuf, currentName))
        {
            if (ImGui::Selectable("(none)", src.PropertyName.empty()))
            {
                src.PropertyName.clear();
                changed = true;
            }
            for (const auto& p : props)
            {
                const char* typeLabel = "";
                switch (p.Type)
                {
                case Graphics::PropertyDataType::Scalar: typeLabel = " [float]";
                    break;
                case Graphics::PropertyDataType::Vec3: typeLabel = " [vec3]";
                    break;
                case Graphics::PropertyDataType::Vec4: typeLabel = " [vec4]";
                    break;
                }
                char itemLabel[256];
                snprintf(itemLabel, sizeof(itemLabel), "%s%s", p.Name.c_str(), typeLabel);
                if (ImGui::Selectable(itemLabel, src.PropertyName == p.Name))
                {
                    src.PropertyName = p.Name;
                    src.AutoRange = true;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    if (src.PropertyName.empty())
        return changed;

    // Colormap selector.
    snprintf(idBuf, sizeof(idBuf), "Colormap##%s", suffix);
    int mapIdx = static_cast<int>(src.Map);
    const char* mapNames[] = {"Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
    if (ImGui::Combo(idBuf, &mapIdx, mapNames, 6))
    {
        src.Map = static_cast<Graphics::Colormap::Type>(mapIdx);
        changed = true;
    }

    // Auto-range checkbox.
    snprintf(idBuf, sizeof(idBuf), "Auto Range##%s", suffix);
    if (ImGui::Checkbox(idBuf, &src.AutoRange))
        changed = true;

    // Range sliders (disabled when auto-range is on).
    if (!src.AutoRange)
    {
        snprintf(idBuf, sizeof(idBuf), "Range Min##%s", suffix);
        if (ImGui::DragFloat(idBuf, &src.RangeMin, 0.01f))
            changed = true;
        snprintf(idBuf, sizeof(idBuf), "Range Max##%s", suffix);
        if (ImGui::DragFloat(idBuf, &src.RangeMax, 0.01f))
            changed = true;
    }
    else
    {
        ImGui::Text("Range: [%.4f, %.4f]", src.RangeMin, src.RangeMax);
    }

    // Bins slider.
    snprintf(idBuf, sizeof(idBuf), "Bins##%s", suffix);
    int bins = static_cast<int>(src.Bins);
    if (ImGui::SliderInt(idBuf, &bins, 0, 32, bins == 0 ? "Continuous" : "%d"))
    {
        src.Bins = static_cast<uint32_t>(std::max(0, bins));
        changed = true;
    }

    return changed;
}

// =========================================================================
// VectorFieldWidget
// =========================================================================
bool VectorFieldWidget(Graphics::VisualizationConfig& config,
                       const Geometry::PropertySet* ps, const char* suffix)
{
    bool changed = false;
    char idBuf[128];

    ImGui::SeparatorText("Vector Fields");

    // Available vec3 properties.
    std::vector<Graphics::PropertyInfo> vecProps;
    if (ps)
        vecProps = Graphics::EnumerateVectorProperties(*ps);

    // Add new vector field.
    snprintf(idBuf, sizeof(idBuf), "Add Vector Field##%s", suffix);
    if (!vecProps.empty() && ImGui::BeginCombo(idBuf, "Add..."))
    {
        for (const auto& p : vecProps)
        {
            if (ImGui::Selectable(p.Name.c_str()))
            {
                Graphics::VectorFieldEntry entry;
                entry.PropertyName = p.Name;
                config.VectorFields.push_back(std::move(entry));
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    // List existing vector fields.
    for (size_t i = 0; i < config.VectorFields.size();)
    {
        auto& vf = config.VectorFields[i];
        ImGui::PushID(static_cast<int>(i));

        ImGui::Text("%s", vf.PropertyName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
        {
            config.VectorFields.erase(config.VectorFields.begin() + static_cast<ptrdiff_t>(i));
            changed = true;
            ImGui::PopID();
            continue;
        }

        ImGui::DragFloat("Scale", &vf.Scale, 0.01f, 0.001f, 100.0f);
        ImGui::SliderFloat("Width", &vf.EdgeWidth, 0.5f, 5.0f);
        float vc[4] = {vf.Color.r, vf.Color.g, vf.Color.b, vf.Color.a};
        if (ImGui::ColorEdit4("Color", vc))
            vf.Color = glm::vec4(vc[0], vc[1], vc[2], vc[3]);
        ImGui::Checkbox("Overlay", &vf.Overlay);

        // Per-vector color property selector.
        if (ps)
        {
            auto colorableProps = Graphics::EnumerateColorableProperties(*ps);
            const char* colorPreview = vf.ColorPropertyName.empty()
                                           ? "(Uniform)"
                                           : vf.ColorPropertyName.c_str();
            if (ImGui::BeginCombo("Arrow Color", colorPreview))
            {
                if (ImGui::Selectable("(Uniform)", vf.ColorPropertyName.empty()))
                {
                    vf.ColorPropertyName.clear();
                    changed = true;
                }
                for (const auto& cp : colorableProps)
                {
                    if (ImGui::Selectable(cp.Name.c_str(), vf.ColorPropertyName == cp.Name))
                    {
                        vf.ColorPropertyName = cp.Name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            // Per-vector length property selector.
            auto scalarProps = Graphics::EnumerateScalarProperties(*ps);
            const char* lenPreview = vf.LengthPropertyName.empty()
                                         ? "(Uniform)"
                                         : vf.LengthPropertyName.c_str();
            if (ImGui::BeginCombo("Arrow Length", lenPreview))
            {
                if (ImGui::Selectable("(Uniform)", vf.LengthPropertyName.empty()))
                {
                    vf.LengthPropertyName.clear();
                    changed = true;
                }
                for (const auto& sp : scalarProps)
                {
                    if (ImGui::Selectable(sp.Name.c_str(), vf.LengthPropertyName == sp.Name))
                    {
                        vf.LengthPropertyName = sp.Name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::PopID();
        ++i;
    }

    return changed;
}

// =========================================================================
// Utility functions
// =========================================================================

bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps)
{
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            if (std::abs(a[c][r] - b[c][r]) > eps)
                return false;
        }
    }
    return true;
}

bool Vec3NearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
}

bool OctreeSettingsEqual(const Graphics::OctreeDebugDrawSettings& a,
                         const Graphics::OctreeDebugDrawSettings& b)
{
    return a.Enabled == b.Enabled &&
        a.Overlay == b.Overlay &&
        a.ColorByDepth == b.ColorByDepth &&
        a.MaxDepth == b.MaxDepth &&
        a.LeafOnly == b.LeafOnly &&
        a.DrawInternal == b.DrawInternal &&
        a.OccupiedOnly == b.OccupiedOnly &&
        std::abs(a.Alpha - b.Alpha) <= 1e-4f &&
        Vec3NearlyEqual(a.BaseColor, b.BaseColor);
}

glm::vec3 DepthRamp(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    constexpr std::array<glm::vec3, 5> k = {
        glm::vec3{0.267f, 0.005f, 0.329f},
        glm::vec3{0.230f, 0.322f, 0.546f},
        glm::vec3{0.128f, 0.566f, 0.550f},
        glm::vec3{0.369f, 0.788f, 0.382f},
        glm::vec3{0.993f, 0.906f, 0.144f},
    };

    const float x = t * 4.0f;
    const int i0 = std::clamp(static_cast<int>(x), 0, 3);
    const int i1 = i0 + 1;
    const float alpha = x - static_cast<float>(i0);
    return k[i0] * (1.0f - alpha) + k[i1] * alpha;
}

uint32_t PackWithAlpha(const glm::vec3& rgb, float alpha)
{
    return Graphics::DebugDraw::PackColorF(rgb.r, rgb.g, rgb.b, alpha);
}

void TransformAABB(const glm::vec3& lo, const glm::vec3& hi, const glm::mat4& m,
                   glm::vec3& outLo, glm::vec3& outHi)
{
    glm::vec3 corners[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
        {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
        {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
    };

    outLo = glm::vec3(std::numeric_limits<float>::max());
    outHi = glm::vec3(std::numeric_limits<float>::lowest());

    for (const glm::vec3& corner : corners)
    {
        const glm::vec3 transformed = glm::vec3(m * glm::vec4(corner, 1.0f));
        outLo = glm::min(outLo, transformed);
        outHi = glm::max(outHi, transformed);
    }
}

} // namespace Runtime::EditorUI
