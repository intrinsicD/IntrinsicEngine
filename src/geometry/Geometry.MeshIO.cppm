module;

#include <string_view>

export module Geometry.MeshIO;

import Geometry.Properties;
import Core.Error;

export namespace Geometry::MeshIO
{
    struct MeshIOResult
    {
        PropertySet Vertices{};
        PropertySet Halfedges{};
        PropertySet Edges{};
        PropertySet Faces{};

        std::string_view SourcePath;       // Original file path (for error messages)
        std::string_view BasePath;         // Directory containing the file (for relative refs)
    };

    Core::Expected<MeshIOResult> LoadOBJ(std::string_view absolute_path);
    Core::Expected<MeshIOResult> LoadOFF(std::string_view absolute_path);
    Core::Expected<MeshIOResult> LoadPLY(std::string_view absolute_path);
    Core::Expected<MeshIOResult> LoadSTL(std::string_view absolute_path);
    // and other missing formats for meshes only. Complete models or scenes will be handled differently?
}