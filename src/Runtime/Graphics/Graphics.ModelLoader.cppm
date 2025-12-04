module;
#include <string>
#include <memory>

export module Runtime.Graphics.ModelLoader;

import Runtime.Graphics.Model;
import Runtime.RHI.Device;

export namespace Runtime::Graphics {
    
    class ModelLoader {
    public:
        // Loads a GLTF file and returns a list of Meshes (Primitives)
        static std::shared_ptr<Model> Load(std::shared_ptr<RHI::VulkanDevice> device, const std::string& filepath);
    };
}