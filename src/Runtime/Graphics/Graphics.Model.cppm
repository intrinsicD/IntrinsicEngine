module;
#include <vector>
#include <memory>
#include <string>

export module Graphics:Model;

import :Geometry;
import :Material; // Optional, if we want to store materials here later
import Geometry;
import RHI;

export namespace Graphics
{

    struct MeshSegment
    {
        Geometry::GeometryHandle Handle;
        std::shared_ptr<GeometryCollisionData> CollisionGeometry;
        std::string Name;
    };

    struct Model
    {
        Model(GeometryPool& storage, std::shared_ptr<RHI::VulkanDevice> device)
            : m_Storage(storage), m_Device(std::move(device)) {}

        // RAII: Release handles on destruction using deferred deletion
        ~Model() {
            // Get the current global frame number for deferred deletion
            uint64_t currentFrame = m_Device ? m_Device->GetGlobalFrameNumber() : 0;

            for(auto& mesh : Meshes) {
                if(mesh->Handle.IsValid()) {
                    m_Storage.Remove(mesh->Handle, currentFrame);
                }
            }
        }

        // The individual parts of the model (primitives)
        std::vector<std::shared_ptr<MeshSegment>> Meshes;

        [[nodiscard]] bool IsValid() const { return !Meshes.empty(); }
        [[nodiscard]] size_t Size() const { return Meshes.size(); }

    private:
        GeometryPool& m_Storage;
        std::shared_ptr<RHI::VulkanDevice> m_Device;
    };
}
