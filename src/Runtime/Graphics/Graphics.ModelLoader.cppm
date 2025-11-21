module;
#include <string>
#include <vector>
#include <memory>

export module Runtime.Graphics.ModelLoader;

import Runtime.Graphics.Mesh;
import Runtime.RHI.Device;

export namespace Runtime::Graphics {
    
    class ModelLoader {
    public:
        // Loads a GLTF file and returns a list of Meshes (Primitives)
        static std::vector<std::unique_ptr<Mesh>> Load(Runtime::RHI::VulkanDevice& device, const std::string& filepath);
    };
}