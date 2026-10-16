module;
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.SceneEditingOperations;
import Geometry.IO;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.PointCloud;
import Geometry.PointCloud.IO;
import Geometry.Graph;
import Geometry.Graph.IO;
import Geometry.Properties;
import Geometry.Validation;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace IO = Geometry::IO;
        using Format = IO::GeometryIOFormatKind;
        using RetainedProperties = std::map<const Geometry::PropertySet*, std::set<std::string>>;
        constexpr auto Invalid = std::numeric_limits<std::uint32_t>::max();

        std::optional<IO::GeometryIODomain> ExportDomain(const GS::ConstSourceView& view)
        {
            switch (GS::BuildSourceAvailability(view).ProvenanceDomain)
            {
            case GS::Domain::Mesh:       return IO::GeometryIODomain::Mesh;
            case GS::Domain::PointCloud: return IO::GeometryIODomain::PointCloud;
            case GS::Domain::Graph:      return IO::GeometryIODomain::Graph;
            default:                     return std::nullopt;
            }
        }

        bool ExportView(const EditorSceneEditingCommands& commands,
            std::uint32_t id,
            GS::ConstSourceView& view,
            std::string& reason)
        {
            const auto* context = EditorSceneEditingCommandsAccess::Resolve(commands);
            if (!context || !context->Scene || (context->AttachmentActive && !context->AttachmentActive()))
            {
                reason = "Geometry export requires an active scene.";
                return false;
            }
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context->Scene->Raw(), id);
            if (!entity)
            {
                reason = "Select a live geometry entity to export.";
                return false;
            }
            view = GS::BuildConstView(context->Scene->Raw(), *entity);
            if (!ExportDomain(view))
            {
                reason = "Selected entity has no supported geometry domain.";
                return false;
            }
            return true;
        }

        std::string FormatReason(const GS::ConstSourceView& view, const IO::GeometryIOFormatInfo& format)
        {
            const auto domain = ExportDomain(view);
            return domain && IO::SupportsExportDomain(format.CanonicalExtension, *domain)
                ? std::string{}
                : "This format cannot export the selected geometry domain.";
        }

        bool LiveRows(const Geometry::PropertySet& properties,
            std::string_view maskName,
            std::vector<std::uint32_t>& rows,
            std::string& reason)
        {
            const auto deleted = properties.Get<bool>(maskName);
            if (properties.Size() > Invalid ||
                (properties.Exists(maskName) && (!deleted || deleted.Vector().size() != properties.Size())))
            {
                reason = "Geometry has an invalid deletion mask or too many rows.";
                return false;
            }
            for (std::size_t i = 0; i < properties.Size(); ++i)
                if (!deleted || !deleted[i])
                    rows.push_back(static_cast<std::uint32_t>(i));
            return true;
        }

        template <class T>
        bool CopyChannel(const Geometry::PropertySet& source,
            std::string_view name,
            Geometry::PropertySet& target,
            std::string_view output,
            const std::vector<std::uint32_t>& rows,
            RetainedProperties& kept,
            std::string& reason)
        {
            if (!source.Exists(name))
                return true;
            const auto values = source.Get<T>(name);
            if (!values || values.Vector().size() != source.Size())
            {
                reason = "Export property has the wrong type or count: " + std::string{name};
                return false;
            }
            auto destination = target.GetOrAdd<T>(std::string{output});
            destination.Vector().clear();
            for (const auto row : rows)
            {
                const auto& value = values[row];
                bool valid;
                if constexpr (std::is_same_v<T, std::string>)
                    valid = value.find_first_of("\r\n") == std::string::npos;
                else if constexpr (std::is_same_v<T, glm::vec4>)
                    valid = Geometry::Validation::IsFinite(glm::vec3{value}) && std::isfinite(value.w);
                else
                    valid = Geometry::Validation::IsFinite(value);
                if (!valid)
                {
                    reason = "Export property contains an invalid value: " + std::string{name};
                    return false;
                }
                destination.Vector().push_back(value);
            }
            kept[&source].insert(std::string{name});
            return true;
        }

        void ReportDropped(
            const GS::ConstSourceView& view, const RetainedProperties& kept, EditorGeometryExportResult& result)
        {
            const auto report = [&](const auto* source, std::string_view domain)
            {
                if (!source)
                    return;
                const auto retained = kept.find(&source->Properties);
                for (const auto& name : source->Properties.Properties())
                    if (retained == kept.end() || !retained->second.contains(name))
                        result.DroppedProperties.push_back(std::string{domain} + "/" + name);
            };
            report(view.VertexSource, "vertex");
            report(view.HalfedgeSource, "halfedge");
            report(view.EdgeSource, "edge");
            report(view.FaceSource, "face");
        }

        bool ProjectMesh(const GS::ConstSourceView& view,
            Format format,
            const std::vector<std::uint32_t>& rows,
            Geometry::MeshIO::MeshIOResult& mesh,
            RetainedProperties& kept,
            EditorGeometryExportResult& result)
        {
            if (!view.HalfedgeSource || !view.FaceSource)
            {
                result.Message = "Mesh export requires halfedge and face sources.";
                return false;
            }
            const auto& vertices = view.VertexSource->Properties;
            const auto& halfedges = view.HalfedgeSource->Properties;
            const auto& faces = view.FaceSource->Properties;
            const auto next = halfedges.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext);
            const auto to = halfedges.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeToVertex);
            const auto face = halfedges.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeFace);
            const auto first = faces.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge);
            if (!next || !to || !face || !first || next.Vector().size() != halfedges.Size() ||
                to.Vector().size() != halfedges.Size() || face.Vector().size() != halfedges.Size() ||
                first.Vector().size() != faces.Size())
            {
                result.Message = "Mesh export requires count-matched canonical topology.";
                return false;
            }
            mesh.Vertices.Resize(rows.size());
            if (!CopyChannel<glm::vec3>(
                    vertices, GS::PropertyNames::kPosition, mesh.Vertices, "v:point", rows, kept, result.Message))
                return false;
            std::vector<std::uint32_t> map(vertices.Size(), Invalid), liveFaces, corners;
            for (std::uint32_t i = 0; i < rows.size(); ++i)
                map[rows[i]] = i;
            if (!LiveRows(faces, "f:deleted", liveFaces, result.Message))
                return false;
            std::vector<std::vector<std::uint32_t>> polygons;
            for (const auto f : liveFaces)
            {
                std::vector<std::uint32_t> ring;
                using namespace GeometryProcessingDetail::MeshSupport;
                const auto status = BuildMeshFaceRing(first.Vector(),
                    face.Vector(),
                    next.Vector(),
                    to.Vector(),
                    f,
                    static_cast<std::uint32_t>(vertices.Size()),
                    ring);
                if (status != MeshFaceRingStatus::Triangulate || (format == Format::STL && ring.size() != 3))
                {
                    result.Message = format == Format::STL ? "STL export requires triangle faces."
                                                           : "Mesh export found an invalid live face ring.";
                    return false;
                }
                auto h = first[f];
                for (auto& vertex : ring)
                {
                    if (map[vertex] == Invalid)
                    {
                        result.Message = "A live face references a deleted vertex.";
                        return false;
                    }
                    vertex = map[vertex];
                    corners.push_back(h);
                    h = next[h];
                }
                polygons.push_back(std::move(ring));
            }
            mesh.Faces.Resize(polygons.size());
            mesh.Faces.GetOrAdd<std::vector<std::uint32_t>>("f:vertices").Vector() = std::move(polygons);
            mesh.Halfedges.Resize(corners.size());
            if (format == Format::OBJ || format == Format::PLY)
            {
                if (!(format == Format::OBJ && halfedges.Exists("h:normal")) &&
                    !CopyChannel<glm::vec3>(
                        vertices, "v:normal", mesh.Vertices, "v:normal", rows, kept, result.Message))
                    return false;
                if (!(format == Format::OBJ && halfedges.Exists("h:texcoord")) &&
                    !CopyChannel<glm::vec2>(
                        vertices, "v:texcoord", mesh.Vertices, "v:texcoord", rows, kept, result.Message))
                    return false;
                if (!CopyChannel<glm::vec4>(vertices, "v:color", mesh.Vertices, "v:color", rows, kept, result.Message))
                    return false;
                if (vertices.Exists("v:color"))
                    result.Losses.push_back(
                        "Vertex color alpha is omitted; RGB may be clamped/quantized by the format.");
            }
            if (format == Format::OBJ &&
                (!CopyChannel<glm::vec3>(
                     halfedges, "h:normal", mesh.Halfedges, "h:normal", corners, kept, result.Message) ||
                    !CopyChannel<glm::vec2>(
                        halfedges, "h:texcoord", mesh.Halfedges, "h:texcoord", corners, kept, result.Message)))
                return false;
            if (format == Format::STL)
                result.Losses.push_back(
                    "STL stores triangle soup: shared vertex identity and polygon topology are not retained.");
            return true;
        }

        bool ProjectCloud(const GS::ConstSourceView& view,
            Format format,
            const std::vector<std::uint32_t>& rows,
            Geometry::PointCloudIO::PointCloudIOResult& cloud,
            RetainedProperties& kept,
            EditorGeometryExportResult& result)
        {
            const auto& vertices = view.VertexSource->Properties;
            Geometry::PropertySet properties;
            properties.Resize(rows.size());
            if (!CopyChannel<glm::vec3>(
                    vertices, GS::PropertyNames::kPosition, properties, "v:point", rows, kept, result.Message))
                return false;
            if (format != Format::XYZ &&
                !CopyChannel<glm::vec3>(vertices, "v:normal", properties, "p:normal", rows, kept, result.Message))
                return false;
            if (!CopyChannel<glm::vec4>(vertices, "p:color", properties, "p:color", rows, kept, result.Message))
                return false;
            if (format == Format::PLY &&
                !CopyChannel<float>(vertices, "p:radius", properties, "p:radius", rows, kept, result.Message))
                return false;
            if (vertices.Exists("p:color"))
                result.Losses.push_back("Point color alpha is omitted; RGB may be clamped/quantized by the format.");
            Geometry::PointCloud::Cloud snapshot{properties};
            cloud.Cloud = snapshot;
            return true;
        }

        bool ProjectGraph(const GS::ConstSourceView& view,
            Format format,
            const std::vector<std::uint32_t>& rows,
            Geometry::GraphIO::GraphIOResult& graph,
            RetainedProperties& kept,
            EditorGeometryExportResult& result)
        {
            if (!view.EdgeSource)
            {
                result.Message = "Graph export requires an edge source.";
                return false;
            }
            const auto& vertices = view.VertexSource->Properties;
            const auto& edges = view.EdgeSource->Properties;
            const auto positions = vertices.Get<glm::vec3>(GS::PropertyNames::kPosition);
            const auto v0 = edges.Get<std::uint32_t>(GS::PropertyNames::kEdgeV0);
            const auto v1 = edges.Get<std::uint32_t>(GS::PropertyNames::kEdgeV1);
            if (!v0 || !v1 || v0.Vector().size() != edges.Size() || v1.Vector().size() != edges.Size())
            {
                result.Message = "Graph export requires count-matched edge endpoints.";
                return false;
            }
            std::vector<std::uint32_t> map(vertices.Size(), Invalid), liveEdges;
            for (const auto row : rows)
                map[row] = graph.Graph.AddVertex(positions[row]).Index;
            if (!LiveRows(edges, "e:deleted", liveEdges, result.Message))
                return false;
            for (const auto edge : liveEdges)
            {
                const auto a = v0[edge], b = v1[edge];
                if (a >= map.size() || b >= map.size() || map[a] == Invalid || map[b] == Invalid ||
                    !graph.Graph.AddEdge(Geometry::VertexHandle{map[a]}, Geometry::VertexHandle{map[b]}))
                {
                    result.Message = "Graph export found an invalid live edge.";
                    return false;
                }
            }
            if (!CopyChannel<float>(
                    edges, "e:weight", graph.Graph.EdgeProperties(), "e:weight", liveEdges, kept, result.Message))
                return false;
            if (!CopyChannel<std::string>(
                    edges, "e:label", graph.Graph.EdgeProperties(), "e:label", liveEdges, kept, result.Message))
                return false;
            if (format == Format::TGF)
            {
                if (!CopyChannel<std::string>(
                        vertices, "v:label", graph.Graph.VertexProperties(), "v:label", rows, kept, result.Message))
                    return false;
            }
            else
                result.Losses.push_back("Edge lists omit positions, vertex labels and isolated vertices.");
            return true;
        }
    }

    std::vector<EditorGeometryExportFormat> GetEditorGeometryExportFormats(
        const EditorSceneEditingCommands& commands, const std::uint32_t stableEntityId)
    {
        GS::ConstSourceView view;
        std::string reason;
        ExportView(commands, stableEntityId, view, reason);
        std::vector<EditorGeometryExportFormat> formats;
        for (const auto& format : IO::SupportedGeometryIOFormats())
        {
            if (format.ExportDomains.empty())
                continue;
            const auto disabled = reason.empty() ? FormatReason(view, format) : reason;
            formats.push_back({std::string{format.CanonicalExtension}, disabled.empty(), disabled});
        }
        return formats;
    }

    EditorGeometryExportResult ApplyEditorGeometryExportCommand(
        const EditorSceneEditingCommands& commands, const EditorGeometryExportCommand& command)
    {
        EditorGeometryExportResult result{.Status = EditorCommandStatus::InvalidProcessingParameters};
        GS::ConstSourceView view;
        if (!ExportView(commands, command.StableEntityId, view, result.Message))
            return result;
        const auto* format = IO::FindGeometryIOFormat(command.Format);
        if (!format)
        {
            result.Message = "Unknown geometry export format.";
            return result;
        }
        result.Message = FormatReason(view, *format);
        if (!result.Message.empty())
            return result;
        const std::filesystem::path destination{command.Path};
        if (command.Path.empty() || command.Path.find('\0') != std::string::npos ||
            IO::FindGeometryIOFormat(destination.extension().string()) != format)
        {
            result.Message = "Choose a destination path with the selected format's extension.";
            return result;
        }
        if (!view.VertexSource)
        {
            result.Message = "Geometry export requires a vertex source.";
            return result;
        }
        const auto& vertices = view.VertexSource->Properties;
        const auto positions = vertices.Get<glm::vec3>(GS::PropertyNames::kPosition);
        if (!positions || positions.Vector().size() != vertices.Size())
        {
            result.Message = "Geometry export requires count-matched canonical positions.";
            return result;
        }
        std::vector<std::uint32_t> rows;
        if (!LiveRows(vertices, "v:deleted", rows, result.Message))
            return result;
        if (rows.empty())
        {
            result.Message = "Geometry export requires live vertices.";
            return result;
        }
        for (const auto row : rows)
            if (!Geometry::Validation::IsFinite(positions[row]))
            {
                result.Message = "Geometry export requires finite live positions.";
                return result;
            }
        RetainedProperties kept;
        const auto retainTopology = [&](const auto* source, std::initializer_list<std::string_view> names)
        {
            if (source)
                for (const auto name : names)
                    kept[&source->Properties].insert(std::string{name});
        };
        retainTopology(
            view.VertexSource, {GS::PropertyNames::kPosition, "v:point", "v:deleted", "p:deleted", "v:connectivity"});
        retainTopology(view.HalfedgeSource,
            {"h:connectivity",
                GS::PropertyNames::kHalfedgeToVertex,
                GS::PropertyNames::kHalfedgeNext,
                GS::PropertyNames::kHalfedgeFace});
        retainTopology(view.EdgeSource, {GS::PropertyNames::kEdgeV0, GS::PropertyNames::kEdgeV1, "e:deleted"});
        retainTopology(view.FaceSource, {GS::PropertyNames::kFaceHalfedge, "f:deleted"});
        Geometry::MeshIO::MeshIOResult mesh;
        Geometry::PointCloudIO::PointCloudIOResult cloud;
        Geometry::GraphIO::GraphIOResult graph;
        const auto domain = *ExportDomain(view);
        if (domain == IO::GeometryIODomain::Mesh && !ProjectMesh(view, format->Kind, rows, mesh, kept, result))
            return result;
        if (domain == IO::GeometryIODomain::PointCloud && !ProjectCloud(view, format->Kind, rows, cloud, kept, result))
            return result;
        if (domain == IO::GeometryIODomain::Graph && !ProjectGraph(view, format->Kind, rows, graph, kept, result))
            return result;
        ReportDropped(view, kept, result);
        // Write beside the destination, then replace only after the writer has succeeded.
        static std::atomic<std::uint64_t> sequence{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto directory = destination;
        directory += ".intrinsic-export-" + std::to_string(stamp) + "-" + std::to_string(sequence++);
        std::error_code error;
        if (!std::filesystem::create_directory(directory, error))
        {
            result.Message = "Cannot create the export staging directory: " + error.message();
            return result;
        }
        struct Cleanup
        {
            std::filesystem::path Path;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove_all(Path, ignored);
            }
        } cleanup{directory};
        const auto path = (directory / destination.filename()).string();
        bool written = false;
        if (domain == IO::GeometryIODomain::Mesh)
        {
            using S = Geometry::MeshIO::MeshIOWriteStatus;
            switch (format->Kind)
            {
            case Format::OBJ: written = Geometry::MeshIO::WriteOBJ(path, mesh) == S::Success; break;
            case Format::OFF: written = Geometry::MeshIO::WriteOFF(path, mesh) == S::Success; break;
            case Format::PLY: written = Geometry::MeshIO::WritePLY(path, mesh) == S::Success; break;
            case Format::STL: written = Geometry::MeshIO::WriteSTL(path, mesh) == S::Success; break;
            default:          break;
            }
        }
        else if (domain == IO::GeometryIODomain::PointCloud)
        {
            using S = Geometry::PointCloudIO::PointCloudIOWriteStatus;
            switch (format->Kind)
            {
            case Format::PLY: written = Geometry::PointCloudIO::WritePLY(path, cloud) == S::Success; break;
            case Format::XYZ: written = Geometry::PointCloudIO::WriteXYZ(path, cloud) == S::Success; break;
            case Format::PCD: written = Geometry::PointCloudIO::WritePCD(path, cloud) == S::Success; break;
            default:          break;
            }
        }
        else
            written = format->Kind == Format::TGF
                ? Geometry::GraphIO::WriteTGF(path, graph) == Geometry::GraphIO::GraphIOWriteStatus::Success
                : Geometry::GraphIO::WriteEdgeList(path, graph) == Geometry::GraphIO::GraphIOWriteStatus::Success;
        if (!written)
        {
            result.Message = "Geometry writer rejected the payload or could not write the file; destination preserved.";
            return result;
        }
        std::filesystem::rename(path, destination, error);
        if (error)
        {
            result.Message = "Cannot replace the export destination: " + error.message();
            return result;
        }
        result.Status = EditorCommandStatus::Applied;
        result.Message = "Exported committed geometry in object space to " + command.Path;
        return result;
    }
}
