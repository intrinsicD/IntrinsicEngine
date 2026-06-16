module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <xatlas.h>

module Geometry.UvAtlas;

import Geometry.Properties;
import Geometry.MeshSoup;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Parameterization.Diagnostics;

namespace Geometry::UvAtlas
{
    namespace
    {
        constexpr double kDegenerateAreaEpsilon = 1.0e-12;
        constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        [[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool AllUvsFinite(const std::span<const glm::vec2> uvs) noexcept
        {
            return std::all_of(uvs.begin(), uvs.end(), [](const glm::vec2 uv) { return IsFinite(uv); });
        }

        [[nodiscard]] double TriangleArea(
            const std::span<const glm::vec3> positions,
            const MeshSoup::PolygonFace& face) noexcept
        {
            const glm::vec3 p0 = positions[face.Indices[0]];
            const glm::vec3 p1 = positions[face.Indices[1]];
            const glm::vec3 p2 = positions[face.Indices[2]];
            return 0.5 * static_cast<double>(glm::length(glm::cross(p1 - p0, p2 - p0)));
        }

        [[nodiscard]] UvAtlasDiagnostics MakeDiagnostics(const UvAtlasInput& input)
        {
            UvAtlasDiagnostics diagnostics{};
            diagnostics.InputVertexCount = input.Positions.size();
            diagnostics.InputFaceCount = input.Faces.size();
            return diagnostics;
        }

        [[nodiscard]] UvAtlasResult MakeFailure(
            const UvAtlasInput& input,
            const UvAtlasStatus status,
            const std::string_view backendName,
            const std::string_view detail = {})
        {
            UvAtlasResult result{};
            result.Status = status;
            result.Provenance = UvAtlasProvenance::None;
            result.Diagnostics = MakeDiagnostics(input);
            result.Diagnostics.Status = status;
            result.Diagnostics.Provenance = UvAtlasProvenance::None;
            result.Diagnostics.BackendName = std::string{backendName};
            result.Diagnostics.BackendDetail = std::string{detail};
            return result;
        }

        [[nodiscard]] bool ExtractTriangleIndices(
            const UvAtlasInput& input,
            std::vector<std::uint32_t>& indices)
        {
            indices.clear();
            indices.reserve(input.Faces.size() * 3u);
            for (const MeshSoup::PolygonFace& face : input.Faces)
            {
                if (face.Indices.size() != 3u)
                {
                    return false;
                }
                for (const MeshSoup::Index index : face.Indices)
                {
                    if (index >= input.Positions.size())
                    {
                        return false;
                    }
                    indices.push_back(index);
                }
            }
            return true;
        }

        [[nodiscard]] std::vector<glm::vec2> ExtractTexcoords(const MeshSoup::IndexedMesh& mesh)
        {
            std::vector<glm::vec2> texcoords(mesh.VertexCount(), glm::vec2{0.0f});
            const auto uvProperty = mesh.VertexProperties().Get<glm::vec2>(MeshUtils::kVertexTexcoordPropertyName);
            if (!uvProperty)
            {
                return texcoords;
            }
            const std::size_t count = std::min(texcoords.size(), uvProperty.Vector().size());
            for (std::size_t i = 0; i < count; ++i)
            {
                texcoords[i] = uvProperty[i];
            }
            return texcoords;
        }

        void FinalizeUvBounds(UvAtlasDiagnostics& diagnostics, const std::span<const glm::vec2> uvs)
        {
            if (uvs.empty())
            {
                diagnostics.NormalizedUvMin = glm::vec2{0.0f};
                diagnostics.NormalizedUvMax = glm::vec2{0.0f};
                return;
            }

            glm::vec2 minUv{std::numeric_limits<float>::max()};
            glm::vec2 maxUv{std::numeric_limits<float>::lowest()};
            for (const glm::vec2 uv : uvs)
            {
                minUv = glm::min(minUv, uv);
                maxUv = glm::max(maxUv, uv);
            }
            diagnostics.NormalizedUvMin = minUv;
            diagnostics.NormalizedUvMax = maxUv;
        }

        [[nodiscard]] Parameterization::ParameterizationDiagnostics EvaluateQuality(
            const MeshSoup::IndexedMesh& mesh)
        {
            std::vector<std::uint32_t> indices;
            indices.reserve(mesh.FaceCount() * 3u);
            for (const MeshSoup::PolygonFace& face : mesh.Faces())
            {
                if (face.Indices.size() != 3u)
                {
                    return {};
                }
                indices.push_back(face.Indices[0]);
                indices.push_back(face.Indices[1]);
                indices.push_back(face.Indices[2]);
            }

            const auto halfedge = MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(mesh.Positions(), indices);
            if (!halfedge)
            {
                return {};
            }

            const std::vector<glm::vec2> texcoords = ExtractTexcoords(mesh);
            return Parameterization::EvaluateParameterizationDiagnostics(*halfedge, texcoords);
        }

        template <class T>
        void CopyTypedProperty(
            const ConstPropertySet& source,
            const std::string& name,
            const std::span<const std::uint32_t> xrefs,
            PropertySet& target,
            VertexPropertyCopyDiagnostics& diagnostics)
        {
            const auto sourceProperty = source.Get<T>(name);
            if (!sourceProperty)
            {
                return;
            }

            auto targetProperty = target.GetOrAdd<T>(name, T{});
            bool copiedAny = false;
            for (std::size_t outputIndex = 0; outputIndex < xrefs.size(); ++outputIndex)
            {
                const std::uint32_t sourceIndex = xrefs[outputIndex];
                if (sourceIndex >= sourceProperty.Vector().size())
                {
                    ++diagnostics.XrefOutOfRangeCount;
                    continue;
                }
                targetProperty[outputIndex] = sourceProperty[sourceIndex];
                copiedAny = true;
            }
            if (copiedAny)
            {
                ++diagnostics.CopiedPropertyCount;
            }
            else
            {
                ++diagnostics.SkippedPropertyCount;
            }
        }

        [[nodiscard]] bool TryCopyKnownPropertyType(
            const ConstPropertySet& source,
            const std::string& name,
            const std::span<const std::uint32_t> xrefs,
            PropertySet& target,
            VertexPropertyCopyDiagnostics& diagnostics)
        {
            const std::size_t copiedBefore = diagnostics.CopiedPropertyCount;
            const std::size_t skippedBefore = diagnostics.SkippedPropertyCount;

            CopyTypedProperty<float>(source, name, xrefs, target, diagnostics);
            CopyTypedProperty<double>(source, name, xrefs, target, diagnostics);
            CopyTypedProperty<std::uint32_t>(source, name, xrefs, target, diagnostics);
            CopyTypedProperty<std::int32_t>(source, name, xrefs, target, diagnostics);
            CopyTypedProperty<bool>(source, name, xrefs, target, diagnostics);
            CopyTypedProperty<glm::vec2>(source, name, xrefs, target, diagnostics);
            CopyTypedProperty<glm::vec3>(source, name, xrefs, target, diagnostics);
            CopyTypedProperty<glm::vec4>(source, name, xrefs, target, diagnostics);

            return diagnostics.CopiedPropertyCount != copiedBefore
                || diagnostics.SkippedPropertyCount != skippedBefore;
        }

        void AttachDiagnostics(
            UvAtlasResult& result,
            const UvAtlasInput& input,
            const UvAtlasOptions& options,
            const std::string_view backendName)
        {
            result.Diagnostics.Status = result.Status;
            result.Diagnostics.Provenance = result.Provenance;
            result.Diagnostics.InputVertexCount = input.Positions.size();
            result.Diagnostics.InputFaceCount = input.Faces.size();
            result.Diagnostics.OutputVertexCount = result.OutputMesh.VertexCount();
            result.Diagnostics.OutputFaceCount = result.OutputMesh.FaceCount();
            if (result.Diagnostics.BackendName.empty())
            {
                result.Diagnostics.BackendName = std::string{backendName};
            }
            if (result.Diagnostics.AtlasWidth == 0u && options.Resolution > 0u && result.Provenance == UvAtlasProvenance::Generated)
            {
                result.Diagnostics.AtlasWidth = options.Resolution;
                result.Diagnostics.AtlasHeight = options.Resolution;
            }
        }

        [[nodiscard]] UvAtlasResult BuildPreservedResult(const UvAtlasInput& input, const UvAtlasDiagnostics& validation)
        {
            UvAtlasResult result{};
            result.Status = UvAtlasStatus::Success;
            result.Provenance = UvAtlasProvenance::AuthoredPreserved;
            result.Diagnostics = validation;
            result.Diagnostics.Status = UvAtlasStatus::Success;
            result.Diagnostics.Provenance = UvAtlasProvenance::AuthoredPreserved;
            result.Diagnostics.BackendName = "authored";
            result.Diagnostics.BackendDetail = "preserved valid authored texcoords";
            result.Diagnostics.PreservedAuthoredUvCount = input.AuthoredTexcoords.size();
            result.Diagnostics.ChartCount = input.Faces.empty() ? 0u : 1u;

            result.SourceVertexForOutputVertex.reserve(input.Positions.size());
            for (std::size_t i = 0; i < input.Positions.size(); ++i)
            {
                (void)result.OutputMesh.AddVertex(input.Positions[i]);
                result.SourceVertexForOutputVertex.push_back(static_cast<std::uint32_t>(i));
            }

            if (input.HasVertexProperties)
            {
                const auto copyDiagnostics = CopySourceVertexPropertiesByXref(
                    input.VertexProperties,
                    result.SourceVertexForOutputVertex,
                    result.OutputMesh.VertexProperties());
                result.Diagnostics.CopiedVertexPropertyCount = copyDiagnostics.CopiedPropertyCount;
                result.Diagnostics.SkippedVertexPropertyCount = copyDiagnostics.SkippedPropertyCount;
                result.Diagnostics.PropertyXrefOutOfRangeCount = copyDiagnostics.XrefOutOfRangeCount;
            }

            auto texcoords = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
                MeshUtils::kVertexTexcoordPropertyName,
                glm::vec2{0.0f});
            for (std::size_t i = 0; i < input.AuthoredTexcoords.size(); ++i)
            {
                texcoords.Vector()[i] = input.AuthoredTexcoords[i];
            }

            result.SourceFaceForOutputFace.reserve(input.Faces.size());
            result.OutputFaceChart.reserve(input.Faces.size());
            for (std::size_t faceIndex = 0; faceIndex < input.Faces.size(); ++faceIndex)
            {
                const MeshSoup::PolygonFace& face = input.Faces[faceIndex];
                (void)result.OutputMesh.AddTriangle(face.Indices[0], face.Indices[1], face.Indices[2]);
                result.SourceFaceForOutputFace.push_back(static_cast<std::uint32_t>(faceIndex));
                result.OutputFaceChart.push_back(0u);
            }

            FinalizeUvBounds(result.Diagnostics, input.AuthoredTexcoords);
            result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
            result.Diagnostics.OutputVertexCount = result.OutputMesh.VertexCount();
            result.Diagnostics.OutputFaceCount = result.OutputMesh.FaceCount();
            return result;
        }

        [[nodiscard]] UvAtlasResult GenerateWithXAtlas(const UvAtlasInput& input, const UvAtlasOptions& options)
        {
            if (options.CancelRequested)
            {
                return MakeFailure(input, UvAtlasStatus::Cancelled, "xatlas", "cancel requested before xatlas generation");
            }

            UvAtlasDiagnostics validation = ValidateUvAtlasInput(input);
            if (validation.Status != UvAtlasStatus::Success)
            {
                UvAtlasResult failure = MakeFailure(input, validation.Status, "xatlas", "invalid atlas input");
                failure.Diagnostics = validation;
                failure.Diagnostics.BackendName = "xatlas";
                return failure;
            }

            std::vector<std::uint32_t> indices;
            if (!ExtractTriangleIndices(input, indices))
            {
                return MakeFailure(input, UvAtlasStatus::BackendRejectedInput, "xatlas", "failed to extract triangle indices");
            }

            xatlas::Atlas* atlas = xatlas::Create();
            if (atlas == nullptr)
            {
                return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas", "xatlas::Create returned null");
            }

            struct AtlasGuard
            {
                xatlas::Atlas* Atlas{nullptr};
                ~AtlasGuard()
                {
                    if (Atlas != nullptr)
                    {
                        xatlas::Destroy(Atlas);
                    }
                }
            } guard{atlas};

            xatlas::MeshDecl meshDecl{};
            meshDecl.vertexPositionData = input.Positions.data();
            meshDecl.vertexPositionStride = sizeof(glm::vec3);
            meshDecl.vertexCount = static_cast<std::uint32_t>(input.Positions.size());
            meshDecl.indexData = indices.data();
            meshDecl.indexCount = static_cast<std::uint32_t>(indices.size());
            meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

            const bool useAuthoredHints = options.UseAuthoredUvsAsChartHints
                && input.AuthoredTexcoords.size() == input.Positions.size()
                && AllUvsFinite(input.AuthoredTexcoords);
            if (useAuthoredHints)
            {
                meshDecl.vertexUvData = input.AuthoredTexcoords.data();
                meshDecl.vertexUvStride = sizeof(glm::vec2);
            }

            const xatlas::AddMeshError addMeshError = xatlas::AddMesh(atlas, meshDecl);
            if (addMeshError != xatlas::AddMeshError::Success)
            {
                return MakeFailure(input, UvAtlasStatus::BackendRejectedInput, "xatlas", xatlas::StringForEnum(addMeshError));
            }

            xatlas::ChartOptions chartOptions{};
            chartOptions.useInputMeshUvs = useAuthoredHints;
            chartOptions.fixWinding = options.FixWinding;

            xatlas::PackOptions packOptions{};
            packOptions.maxChartSize = options.MaxChartSize;
            packOptions.padding = options.Padding;
            packOptions.texelsPerUnit = options.TexelsPerUnit;
            packOptions.resolution = options.Resolution;
            packOptions.bilinear = options.Bilinear;
            packOptions.blockAlign = options.BlockAlign;
            packOptions.bruteForce = options.BruteForcePacking;
            packOptions.rotateChartsToAxis = options.RotateChartsToAxis;
            packOptions.rotateCharts = options.RotateCharts;

            xatlas::Generate(atlas, chartOptions, packOptions);
            if (atlas->meshCount == 0u || atlas->meshes == nullptr)
            {
                return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas", "xatlas produced no output meshes");
            }
            if (atlas->width == 0u || atlas->height == 0u)
            {
                return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas", "xatlas produced a zero-sized atlas");
            }

            const xatlas::Mesh& output = atlas->meshes[0];
            UvAtlasResult result{};
            result.Status = UvAtlasStatus::Success;
            result.Provenance = UvAtlasProvenance::Generated;
            result.Diagnostics = validation;
            result.Diagnostics.Status = UvAtlasStatus::Success;
            result.Diagnostics.Provenance = UvAtlasProvenance::Generated;
            result.Diagnostics.BackendName = "xatlas";
            result.Diagnostics.BackendDetail = "jpcy/xatlas f700c7790aaa030e794b52ba7791a05c085faf0c";
            result.Diagnostics.ChartCount = atlas->chartCount;
            result.Diagnostics.AtlasWidth = atlas->width;
            result.Diagnostics.AtlasHeight = atlas->height;
            result.Diagnostics.AtlasCount = atlas->atlasCount;
            result.Diagnostics.TexelsPerUnit = atlas->texelsPerUnit;

            std::vector<glm::vec2> outputUvs;
            outputUvs.reserve(output.vertexCount);
            result.SourceVertexForOutputVertex.reserve(output.vertexCount);
            for (std::uint32_t vertexIndex = 0; vertexIndex < output.vertexCount; ++vertexIndex)
            {
                const xatlas::Vertex& vertex = output.vertexArray[vertexIndex];
                if (vertex.xref >= input.Positions.size())
                {
                    return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas", "xatlas output vertex xref out of range");
                }

                (void)result.OutputMesh.AddVertex(input.Positions[vertex.xref]);
                result.SourceVertexForOutputVertex.push_back(vertex.xref);
                outputUvs.emplace_back(
                    vertex.uv[0] / static_cast<float>(atlas->width),
                    vertex.uv[1] / static_cast<float>(atlas->height));
            }

            if (input.HasVertexProperties && options.CopySourceVertexProperties)
            {
                const auto copyDiagnostics = CopySourceVertexPropertiesByXref(
                    input.VertexProperties,
                    result.SourceVertexForOutputVertex,
                    result.OutputMesh.VertexProperties());
                result.Diagnostics.CopiedVertexPropertyCount = copyDiagnostics.CopiedPropertyCount;
                result.Diagnostics.SkippedVertexPropertyCount = copyDiagnostics.SkippedPropertyCount;
                result.Diagnostics.PropertyXrefOutOfRangeCount = copyDiagnostics.XrefOutOfRangeCount;
            }

            auto texcoords = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
                MeshUtils::kVertexTexcoordPropertyName,
                glm::vec2{0.0f});
            for (std::size_t i = 0; i < outputUvs.size(); ++i)
            {
                texcoords.Vector()[i] = outputUvs[i];
            }

            if (output.indexCount % 3u != 0u)
            {
                return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas", "xatlas output index count is not triangular");
            }

            const std::size_t outputFaceCount = output.indexCount / 3u;
            result.SourceFaceForOutputFace.reserve(outputFaceCount);
            result.OutputFaceChart.reserve(outputFaceCount);
            for (std::size_t faceIndex = 0; faceIndex < outputFaceCount; ++faceIndex)
            {
                const std::uint32_t i0 = output.indexArray[faceIndex * 3u + 0u];
                const std::uint32_t i1 = output.indexArray[faceIndex * 3u + 1u];
                const std::uint32_t i2 = output.indexArray[faceIndex * 3u + 2u];
                if (i0 >= output.vertexCount || i1 >= output.vertexCount || i2 >= output.vertexCount)
                {
                    return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas", "xatlas output index out of range");
                }
                (void)result.OutputMesh.AddTriangle(i0, i1, i2);
                result.SourceFaceForOutputFace.push_back(
                    faceIndex < input.Faces.size() ? static_cast<std::uint32_t>(faceIndex) : kInvalidIndex);

                const xatlas::Vertex& firstVertex = output.vertexArray[i0];
                result.OutputFaceChart.push_back(firstVertex.chartIndex >= 0
                    ? static_cast<std::uint32_t>(firstVertex.chartIndex)
                    : kInvalidIndex);
            }

            FinalizeUvBounds(result.Diagnostics, outputUvs);
            result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
            AttachDiagnostics(result, input, options, "xatlas");
            return result;
        }
    } // namespace

    const char* ToString(const UvAtlasStatus status) noexcept
    {
        switch (status)
        {
            case UvAtlasStatus::Success:              return "success";
            case UvAtlasStatus::EmptyInput:           return "empty_input";
            case UvAtlasStatus::MissingPositions:     return "missing_positions";
            case UvAtlasStatus::MissingFaces:         return "missing_faces";
            case UvAtlasStatus::MissingAuthoredUvs:   return "missing_authored_uvs";
            case UvAtlasStatus::NonTriangleFace:      return "non_triangle_face";
            case UvAtlasStatus::OutOfRangeIndex:      return "out_of_range_index";
            case UvAtlasStatus::NonFinitePosition:    return "non_finite_position";
            case UvAtlasStatus::NonFiniteAuthoredUv:  return "non_finite_authored_uv";
            case UvAtlasStatus::DegenerateInput:      return "degenerate_input";
            case UvAtlasStatus::InvalidAuthoredUvs:   return "invalid_authored_uvs";
            case UvAtlasStatus::BackendUnavailable:   return "backend_unavailable";
            case UvAtlasStatus::BackendRejectedInput: return "backend_rejected_input";
            case UvAtlasStatus::BackendFailed:        return "backend_failed";
            case UvAtlasStatus::Cancelled:            return "cancelled";
        }
        return "unknown";
    }

    const char* ToString(const UvAtlasProvenance provenance) noexcept
    {
        switch (provenance)
        {
            case UvAtlasProvenance::None:              return "none";
            case UvAtlasProvenance::AuthoredPreserved: return "authored_preserved";
            case UvAtlasProvenance::Generated:         return "generated";
        }
        return "unknown";
    }

    UvAtlasInput BorrowInput(
        const MeshSoup::IndexedMesh& mesh,
        const std::span<const glm::vec2> authoredTexcoords) noexcept
    {
        return UvAtlasInput{
            .Positions = mesh.Positions(),
            .Faces = mesh.Faces(),
            .AuthoredTexcoords = authoredTexcoords,
            .VertexProperties = mesh.VertexProperties(),
            .HasVertexProperties = true,
        };
    }

    UvAtlasDiagnostics ValidateUvAtlasInput(const UvAtlasInput& input)
    {
        UvAtlasDiagnostics diagnostics = MakeDiagnostics(input);

        if (input.Positions.empty() && input.Faces.empty())
        {
            diagnostics.Status = UvAtlasStatus::EmptyInput;
            return diagnostics;
        }
        if (input.Positions.empty())
        {
            diagnostics.Status = UvAtlasStatus::MissingPositions;
            return diagnostics;
        }
        if (input.Faces.empty())
        {
            diagnostics.Status = UvAtlasStatus::MissingFaces;
            return diagnostics;
        }

        for (const glm::vec3 position : input.Positions)
        {
            if (!IsFinite(position))
            {
                ++diagnostics.NonFinitePositionCount;
            }
        }

        for (const MeshSoup::PolygonFace& face : input.Faces)
        {
            if (face.Indices.size() != 3u)
            {
                ++diagnostics.NonTriangleFaceCount;
                continue;
            }

            bool faceOutOfRange = false;
            bool faceNonFinite = false;
            for (const MeshSoup::Index index : face.Indices)
            {
                if (index >= input.Positions.size())
                {
                    ++diagnostics.OutOfRangeIndexCount;
                    faceOutOfRange = true;
                }
                else if (!IsFinite(input.Positions[index]))
                {
                    faceNonFinite = true;
                }
            }

            if (faceOutOfRange || faceNonFinite)
            {
                continue;
            }
            if (TriangleArea(input.Positions, face) <= kDegenerateAreaEpsilon)
            {
                ++diagnostics.DegenerateFaceCount;
            }
        }

        if (diagnostics.NonTriangleFaceCount > 0u)
        {
            diagnostics.Status = UvAtlasStatus::NonTriangleFace;
        }
        else if (diagnostics.OutOfRangeIndexCount > 0u)
        {
            diagnostics.Status = UvAtlasStatus::OutOfRangeIndex;
        }
        else if (diagnostics.NonFinitePositionCount > 0u)
        {
            diagnostics.Status = UvAtlasStatus::NonFinitePosition;
        }
        else if (diagnostics.DegenerateFaceCount == input.Faces.size())
        {
            diagnostics.Status = UvAtlasStatus::DegenerateInput;
        }
        else
        {
            diagnostics.Status = UvAtlasStatus::Success;
        }
        return diagnostics;
    }

    UvAtlasDiagnostics ValidateAuthoredUvs(const UvAtlasInput& input)
    {
        UvAtlasDiagnostics diagnostics = ValidateUvAtlasInput(input);
        if (diagnostics.Status != UvAtlasStatus::Success)
        {
            return diagnostics;
        }
        if (input.AuthoredTexcoords.empty() || input.AuthoredTexcoords.size() != input.Positions.size())
        {
            diagnostics.Status = UvAtlasStatus::MissingAuthoredUvs;
            return diagnostics;
        }

        for (const glm::vec2 uv : input.AuthoredTexcoords)
        {
            if (!IsFinite(uv))
            {
                ++diagnostics.NonFiniteAuthoredUvCount;
            }
        }
        if (diagnostics.NonFiniteAuthoredUvCount > 0u)
        {
            diagnostics.Status = UvAtlasStatus::NonFiniteAuthoredUv;
            return diagnostics;
        }

        std::vector<std::uint32_t> indices;
        if (!ExtractTriangleIndices(input, indices))
        {
            diagnostics.Status = UvAtlasStatus::InvalidAuthoredUvs;
            return diagnostics;
        }

        const auto halfedge = MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(input.Positions, indices);
        if (!halfedge)
        {
            diagnostics.Status = UvAtlasStatus::InvalidAuthoredUvs;
            return diagnostics;
        }

        diagnostics.Quality = Parameterization::EvaluateParameterizationDiagnostics(
            *halfedge,
            input.AuthoredTexcoords);
        if (diagnostics.Quality.Status != Parameterization::ParameterizationDiagnosticsStatus::Success)
        {
            diagnostics.Status = UvAtlasStatus::InvalidAuthoredUvs;
            return diagnostics;
        }

        FinalizeUvBounds(diagnostics, input.AuthoredTexcoords);
        diagnostics.Status = UvAtlasStatus::Success;
        return diagnostics;
    }

    VertexPropertyCopyDiagnostics CopySourceVertexPropertiesByXref(
        const ConstPropertySet& source,
        const std::span<const std::uint32_t> sourceVertexForOutputVertex,
        PropertySet& target)
    {
        VertexPropertyCopyDiagnostics diagnostics{};

        for (const std::string& name : source.Properties())
        {
            if (name == "v:point" || name == MeshUtils::kVertexTexcoordPropertyName)
            {
                continue;
            }

            if (!TryCopyKnownPropertyType(source, name, sourceVertexForOutputVertex, target, diagnostics))
            {
                ++diagnostics.SkippedPropertyCount;
            }
        }

        return diagnostics;
    }

    UvAtlasBackend DefaultXAtlasBackend() noexcept
    {
        return UvAtlasBackend{.Name = "xatlas", .Generate = &GenerateWithXAtlas};
    }

    UvAtlasResult ResolveUvAtlas(
        const UvAtlasInput& input,
        const UvAtlasOptions& options,
        const UvAtlasBackend* backend)
    {
        if (options.CancelRequested)
        {
            return MakeFailure(input, UvAtlasStatus::Cancelled, options.BackendName, "cancel requested before atlas resolution");
        }

        if (options.PreserveValidAuthoredUvs && !options.ForceRegenerate)
        {
            const UvAtlasDiagnostics authoredValidation = ValidateAuthoredUvs(input);
            if (authoredValidation.Status == UvAtlasStatus::Success)
            {
                UvAtlasResult preserved = BuildPreservedResult(input, authoredValidation);
                AttachDiagnostics(preserved, input, options, "authored");
                return preserved;
            }

            const UvAtlasDiagnostics baseValidation = ValidateUvAtlasInput(input);
            if (baseValidation.Status != UvAtlasStatus::Success)
            {
                UvAtlasResult failure = MakeFailure(input, baseValidation.Status, options.BackendName, "invalid atlas input");
                failure.Diagnostics = baseValidation;
                failure.Diagnostics.BackendName = options.BackendName;
                return failure;
            }
        }

        UvAtlasBackend defaultBackend{};
        if (backend == nullptr)
        {
            defaultBackend = DefaultXAtlasBackend();
            backend = &defaultBackend;
        }
        if (backend->Generate == nullptr)
        {
            return MakeFailure(input, UvAtlasStatus::BackendUnavailable, options.BackendName, "atlas backend has no generate function");
        }

        UvAtlasResult result = backend->Generate(input, options);
        AttachDiagnostics(result, input, options, backend->Name);
        return result;
    }
} // namespace Geometry::UvAtlas
