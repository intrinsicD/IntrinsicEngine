module;
#include <cstddef>
#include <vector>
#include <memory>
#include <string>

#include <glm/glm.hpp>

export module Graphics.Model;

import Graphics.Geometry;
import Graphics.Material; // Optional, if we want to store materials here later
import Geometry.Handle;
import RHI.Device;

export namespace Graphics
{
    struct ImportedTextureImage
    {
        std::string Name{};
        int Width = 0;
        int Height = 0;
        int Components = 0;
        int Bits = 0;
        int PixelType = 0;
        bool AsIs = false;
        std::vector<std::byte> Pixels;
    };

    struct ImportedMaterialTextureRefs
    {
        std::string Name{};
        glm::vec4 BaseColorFactor{1.0f};
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        int BaseColorImage = -1;
        int NormalImage = -1;
        int MetallicRoughnessImage = -1;
        int OcclusionImage = -1;
    };

    struct MeshSegment
    {
        Geometry::GeometryHandle Handle;
        std::shared_ptr<GeometryCollisionData> CollisionGeometry;
        std::string Name;
    };

    struct Model
    {
        // RAII: Release handles on destruction using deferred deletion
        Model(GeometryPool& storage, std::shared_ptr<RHI::VulkanDevice> device)
            : Meshes{}
              , EmbeddedImages{}
              , EmbeddedMaterials{}
              , m_Storage(storage)
              , m_Device(std::move(device))
        {
        }


        ~Model()
        {
            // Get the current global frame number for deferred deletion
            uint64_t currentFrame = m_Device ? m_Device->GetGlobalFrameNumber() : 0;

            for (auto& mesh : Meshes)
            {
                if (mesh->Handle.IsValid())
                {
                    m_Storage.Remove(mesh->Handle, currentFrame);
                }
            }
        }

        // The individual parts of the model (primitives)
        std::vector<std::shared_ptr<MeshSegment>> Meshes;
        std::vector<ImportedTextureImage> EmbeddedImages;
        std::vector<ImportedMaterialTextureRefs> EmbeddedMaterials;

        [[nodiscard]] bool IsValid() const { return !Meshes.empty(); }
        [[nodiscard]] size_t Size() const { return Meshes.size(); }

    private:
        GeometryPool& m_Storage;
        std::shared_ptr<RHI::VulkanDevice> m_Device;
    };
}
