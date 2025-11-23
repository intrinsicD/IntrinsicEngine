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
        static std::vector<std::shared_ptr<Mesh>> Load(RHI::VulkanDevice& device, const std::string& filepath);
    };
}