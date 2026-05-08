module;

#include <string>
#include <string_view>

export module Geometry.Graph.IO;

import Geometry.Graph;
import Core.Error;

export namespace Geometry::GraphIO
{
    struct GraphIOResult
    {
        Graph::Graph Graph{};

        std::string SourcePath;       // Original file path (for error messages)
        std::string BasePath;         // Directory containing the file (for relative refs)
    };

    Core::Expected<GraphIOResult> LoadTGF(std::string_view absolute_path);
    Core::Expected<GraphIOResult> LoadEdgeList(std::string_view absolute_path);
}

