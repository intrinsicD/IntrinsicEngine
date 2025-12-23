module;
#include <vector>
#include <memory>
#include <string>

export module Runtime.Graphics.Model;

import Runtime.Graphics.Geometry;
import Runtime.Graphics.Material; // Optional, if we want to store materials here later

export namespace Runtime::Graphics
{

    struct MeshSegment
    {
        GeometryHandle Handle;
        std::shared_ptr<GeometryCollisionData> CollisionGeometry;
        std::string Name;
    };

    struct Model
    {
        Model(GeometryStorage& storage) : m_Storage(storage) {}

        // RAII: Release handles on destruction
        ~Model() {
            for(auto& mesh : Meshes) {
                if(mesh->Handle.IsValid()) {
                    m_Storage.Remove(mesh->Handle);
                }
            }
        }

        // The individual parts of the model (primitives)
        std::vector<std::shared_ptr<MeshSegment>> Meshes;

        [[nodiscard]] bool IsValid() const { return !Meshes.empty(); }
        [[nodiscard]] size_t Size() const { return Meshes.size(); }

    private:
        GeometryStorage& m_Storage;
    };
}
