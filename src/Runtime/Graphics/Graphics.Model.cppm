module;
#include <vector>
#include <memory>

export module Runtime.Graphics.Model;

import Runtime.Graphics.Geometry;
import Runtime.Graphics.Material; // Optional, if we want to store materials here later

export namespace Runtime::Graphics
{
    struct Model
    {
        // The individual parts of the model (primitives)
        std::vector<std::shared_ptr<GeometryGpuData>> Meshes;

        // Helper to check if loaded
        [[nodiscard]] bool IsValid() const { return !Meshes.empty(); }
        [[nodiscard]] size_t Size() const { return Meshes.size(); }
    };
}
